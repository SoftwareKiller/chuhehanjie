#include "gameServer.h"
#include "getValue.hpp"
#include "DealRequest.hpp"

static int startup(const int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if(sock < 0)
    {
        std::cerr << "socket" << std::endl;
        return -1;
    }
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in local;
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);

    if(bind(sock, (struct sockaddr*)&local, sizeof(local)) < 0)
    {
        std::cerr << "bind" <<  std::endl;
        return -1;
    }
    if(listen(sock, 5) < 0)
    {
        std::cerr << "listen" << std::endl;
        return -1;
    }
    return sock;
}

void ProcessConnect(const int& listen_sock, int& epoll_fd)
{
    struct sockaddr_in client_addr;
    socklen_t len = sizeof(client_addr);
    int connect_fd = accept(listen_sock, (struct sockaddr*)&client_addr, &len);
    if(connect_fd < 0)
    {
        std::cerr << "accept" << std::endl;
        return;
    }
    std::cout << "client " << inet_ntoa(client_addr.sin_addr) << ": " << ntohs(client_addr.sin_port) << std::endl;
    struct epoll_event ev;
    ev.data.fd = connect_fd;
    ev.events = EPOLLIN;
    int ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, connect_fd, &ev);
    if(ret < 0)
    {
        std::cerr << "epoll_ctl" << std::endl;
        return;
    }
}

void ProcessRequest(const unsigned int& connect_fd, const unsigned int& epoll_fd, std::list<OnlineUser>& online,std::list<unsigned int> &MatchQueue, threadpool_t* pool)
{
    /*if(online[connect_fd].Isplaying)
      return;*/
    std::list<OnlineUser>::iterator it = online.begin();

    bool flag = false;
    if(online.size() > 0)
    {
        if(online.size() == 1 && it->sock_fd == connect_fd)
            flag = true;
        while(it != online.end())
        {
            /*if((SocketConnected(it->sock_fd) < 0) && (!it->Isplaying)){
                close(it->sock_fd);
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connect_fd, NULL);
                pthread_mutex_lock(&mutex_online);
                online.erase(it);
                pthread_mutex_unlock(&mutex_online);
            }*/
            if((*it).sock_fd == connect_fd)
            {
                flag = true;
                break;
            }
            it++; 
        }
        if(flag && it->Isplaying)
            return;
    }
    char buf[1024] = {0};
    ssize_t read_size = read(connect_fd, buf,sizeof(buf)-1);

    if(read_size <= 0){
        if(read_size < 0){
            std::cerr << "read err" << std::endl;
        }

        if(MatchQueue.size() > 0){
            std::list<unsigned int>::iterator itQueue = MatchQueue.begin();
            while(itQueue != MatchQueue.end()){
                if(*itQueue == it->sock_fd ){
                    std::cout << "erase MatchQueue" << std::endl;
                    MatchQueue.erase(itQueue);
                    break;
                }
                ++itQueue;
            }
        }

        close(connect_fd);
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connect_fd, NULL);
        if(flag)
        {
            pthread_mutex_lock(&mutex_online);
            online.erase(it);
            pthread_mutex_unlock(&mutex_online);
        }
        std::cout << "online user size:" << online.size() << std::endl;
        std::cout << "client quit" << std::endl;
        return;
    }
    std::string serialized = buf+3;
    UserInfo user;
    EMbattle em;
    /*if(buf[0] == START)
      {
      std::cout << "START" << std::endl;
      Args* parg = new Args;
      parg->online = online;
      parg->MatchQueue = MatchQueue;
      parg->client_fd = connect_fd;
      thread_add_task(pool, Match, parg);
      }*/
    switch(buf[0]){
    case SIGN_IN:
        std::cout << "SIGN_IN" << std::endl;
        getUser(serialized, user);
        Sign_in(user, connect_fd);
        break;
    case LOGIN:
        std::cout << "Login" << std::endl;
        getUser(serialized, user);
        std::cout << "进入Login函数前" << std::endl;
        Login(user, connect_fd, online);
        break;
    case START:
        std::cout << "START" << std::endl;
        //if(buf[0] == START)
        /*{   
          Args* parg = new Args;
          parg->online = online;
          parg->MatchQueue = MatchQueue;
          parg->client_fd = connect_fd;
        //thread_add_task(pool, Match, parg);
        }*/
        Match(online, MatchQueue, connect_fd, pool, epoll_fd);
        break;
    case EMBATTLE:
        std::cout << "EMBATTLE" << std::endl;
        getEmbattle(serialized, em);
        //std::cout << "Test user id" << online[connect_fd].user_id << std::endl;
        Embattle(em, connect_fd, online);
        break;
    default:
        std::cout << "Undefined" << std::endl;
    }
}

int main(int argc, char* argv[])
{
    GOOGLE_PROTOBUF_VERIFY_VERSION;
    if(argc != 2)
    {
        std::cerr << "Usage ./gameServer [port]" << std::endl;
        return 1;
    }
    std::list<OnlineUser> online;
    //std::map<unsigned int, OnlineInfo> online;
    std::list<unsigned int> MatchQueue;
    threadpool_t pool;

    pthread_mutex_init(&mutex_online, NULL);
    threadpool_init(&pool, 10);

    int listen_sock = startup(atoi(argv[1]));
    if(listen_sock < 0 ){
        std::cerr << "startup" << std::endl;
    }

    int epoll_fd = epoll_create(10);
    if(epoll_fd < 0)
    {
        std::cerr << "epoll_create" << std::endl;
        return 5;
    }
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listen_sock;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_sock, &event) < 0)
    {
        std::cerr << "epoll_ctl" << std::endl;    
        return 6;
    }
    for(; ;)
    {
        struct epoll_event events[10];
        int size = epoll_wait(epoll_fd, events, sizeof(events)/sizeof(events[0]), -1);
        if(size < 0)
        {
            std::cerr << "epoll_wait" << std::endl;
            continue;    
        }
        if(size == 0)
        {
            std::cout << "epoll timeout" << std::endl;
            continue;
        }
        for(int i=0; i<size; ++i)
        {
            if(!(events[i].events & EPOLLIN)){
                continue;
            }
            if(events[i].data.fd == listen_sock)//处理监听事件
            {
                ProcessConnect(listen_sock, epoll_fd); 
            }else{//处理connect
                ProcessRequest(events[i].data.fd, epoll_fd, online, MatchQueue, &pool);
            }
        }

    }
    threadpool_destroy(&pool);
    pthread_mutex_destroy(&mutex_online);
    google::protobuf::ShutdownProtobufLibrary();
    return 0;    
}

