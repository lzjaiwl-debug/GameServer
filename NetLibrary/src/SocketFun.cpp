#include "../inc/include.h"

static inline int NetLastError()
{
#if defined(PLATFORM_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

static inline bool NetWouldBlock(int err)
{
#if defined(PLATFORM_WIN32)
    return err == WSAEWOULDBLOCK;
#else
    return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

static inline bool NetFionread(SOCKET fd, int* out_bytes)
{
#if defined(PLATFORM_WIN32)
    u_long avail = 0;
    if (ioctlsocket(fd, FIONREAD, &avail) != 0) return false;
    *out_bytes = (int)avail;
    return true;
#else
    int b = 0;
    if (ioctl(fd, FIONREAD, &b) != 0) return false;
    *out_bytes = b;
    return true;
#endif
}

// 避免与系统/其他库定义冲突，使用命名空间限定或条件定义
#if !defined(htonll) && !defined(_WINSOCK2API_) && !defined(_WS2TCPIP_H)
static inline uint64_t htonll(uint64_t v)
{
    // convert by splitting into two 32-bit words
    uint32_t high = (uint32_t)(v >> 32);
    uint32_t low = (uint32_t)(v & 0xFFFFFFFFULL);
    uint64_t nhigh = (uint64_t)htonl(high); // 确保这里是 htonl(high)
    uint64_t nlow = (uint64_t)htonl(low);   // 确保这里是 htonl(low)
    return (nlow << 32) | nhigh;
}
#endif

#if !defined(ntohll) && !defined(_WINSOCK2API_) && !defined(_WS2TCPIP_H)
static inline uint64_t ntohll(uint64_t v)
{
    // inverse of htonll
    uint32_t high = (uint32_t)(v >> 32);
    uint32_t low = (uint32_t)(v & 0xFFFFFFFFULL);
    uint64_t phigh = (uint64_t)ntohl(high); // 确保这里是 ntohl(high)
    uint64_t plow = (uint64_t)ntohl(low);   // 确保这里是 ntohl(low)
    return (plow << 32) | phigh;
}
#endif
void SocketFun::HandlePiecePack(One<SockPacket>& pack)
{
    Mutex::Lock __(piecepacks_mtx);
    if(piecepacks.Find(pack->get_fd()) >= 0)//首先查看碎片数据缓存中是否有数据
    {
        auto& ppack =  piecepacks.Get(pack->get_fd());//取出数据
        ppack->wirte(*pack, pack->get_wirte_num());//将接收到的数据追加到碎片数据中
        pack = pick(ppack);//将碎片数据所有权移交pack 并释放原pack
        piecepacks.RemoveKey(ppack);//删除碎片数据
    }
}
void SocketFun::HandleBlockPack(PacketFrame* frame, One<SockPacket> pack)
{
    //One<SockPacket> revpack = pick(CreatCopySockPacket(pack));
    //WhenEvent(EventType::READEVENT, revpack, false);
	if(frame->frame.head.buf.MASK)//存在掩码需解码数据
	{
		for (size_t i = 0; i < *pack; ++i) {
            (pack->get_read_ptr_cursor())[i] ^= frame->frame.pack.buf.MASKS[i % 4];
        }
	}
    if(frame->frame.head.buf.FIN == 0 && ((OPCODE)frame->frame.head.buf.OPCODE == OPCODE::TEXTONLY || (OPCODE)frame->frame.head.buf.OPCODE == OPCODE::BINARY))//分包数据
	{
		pack->SetBinary((OPCODE)frame->frame.head.buf.OPCODE == OPCODE::BINARY ? true : false);
        {
            Mutex::Lock __(blockpacks_mtx);
            blockpacks.Add(pack->get_fd(),pick(pack));
        }
		return;
	}
    {
        Mutex::Lock __(blockpacks_mtx);
		if (frame->frame.head.buf.OPCODE == (int)OPCODE::ISBLOCK)//中间分包数据，继续写入分包数据缓存
        {
            pack->RemoveReadSpaces();//重置读取指针位置，准备写入数据
			if (blockpacks.Find(pack->get_fd()) != -1)//存在分块数据缓存
            {
                auto& bpack = blockpacks.Get(pack->get_fd());
                bpack->wirte(*pack, pack->get_wirte_num());
				if (frame->frame.head.buf.FIN)//分包数据接收完毕
                {
					pack = pick(bpack);
                    blockpacks.RemoveKey(pack->get_fd());
					WhenEvent(EventType::PRODATAEVENT, pack, true);//将完整数据抛出
                }
            }
            return;
        }
    }
	WhenEvent(EventType::PRODATAEVENT, pack, true);//不是分包数据直接将完整数据抛出
}
void SocketFun::HandleNoHandshake(One<SockPacket> pack)
{
	WhenEvent(EventType::DATAEVENT, pack, true);//直接将数据抛出
    /*
#ifdef PLATFORM_POSIX
    HandlePiecePack(pack);//首先执行一次碎片数据处理
#else
	if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
        HandlePiecePack(pack);
#endif // DEBUG
	size_t extra_tail_len = 0;
	Httplib::HttpRecvStatus status = Httplib::CheckHttpReceiveCompleteness(*pack, pack->get_wirte_num(), &extra_tail_len);
    if (!status.header_complete && !status.is_chunked && status.need_more)
    {
		// 头未完整且非chunked，且至少还需更多数据，继续接收
#ifdef PLATFORM_POSIX
        {
            Mutex::Lock __(piecepacks_mtx);
            piecepacks.Add(pack->get_fd(), pick(pack)); // 放入碎片缓存
		}
#else
        if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
        {
            Mutex::Lock __(piecepacks_mtx);
            piecepacks.Add(pack->get_fd(), pick(pack)); // 放入碎片缓存
            One<SockPacket> revpack = CreatCopySockPacket(pack);
            WhenEvent(EventType::READEVENT, revpack, true);
        }
        else
        {
            WhenEvent(EventType::READEVENT, pack, true); // Windows 平台直接通知上层继续接收
        }
#endif
		return;
    }
    if (status.header_complete && status.message_complete && status.need_more == 0)
    {
        if (extra_tail_len > 0) {
            One<SockPacket> newpack = CreatCopySockPacket(pack);
            newpack->wirte(pack->offsetPtr(pack->get_wirte_num() - extra_tail_len), extra_tail_len);
            HandleNoHandshake(pick(newpack));
            pack->ReduceToLeft(pack->get_free_space() + extra_tail_len);// 截断多余数据
        }
		WhenEvent(EventType::DATAEVENT, pack, true); // 抛出当前完整数据
        return;
    }
    String s = pack->tostring();
    int headerEnd = s.FindAfter("\r\n\r\n"); // 得到协议头的总大小（包含分隔符）
    if (headerEnd == -1) { // 判定http协议头是否接收完毕
        if (s.Find("HTTP") == -1) {
            WhenEvent(EventType::ERROREVENT, pack, "Not a valid HTTP protocol"); // 协议出现原则性错误
            return;
        }
#ifdef PLATFORM_POSIX
        {
            Mutex::Lock __(piecepacks_mtx);
            piecepacks.Add(pack->get_fd(), pick(pack)); // 没有接收完毕，放入碎片缓存
        }
#else
		if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
        {
            Mutex::Lock __(piecepacks_mtx);
            One<SockPacket> revpack = CreatCopySockPacket(pack);
            piecepacks.Add(pack->get_fd(), pick(pack)); // 没有接收完毕，放入碎片缓存
            WhenEvent(EventType::READEVENT, revpack, true);
        }
        else
        {
            WhenEvent(EventType::READEVENT, pack, true); // Windows 平台直接通知上层继续接收
        }
		
#endif
        return;
    }

    int posCL = s.Find("Content-Length");
    if (posCL == -1) {
        // 无 Content-Length，整个包当作完整数据抛出
        WhenEvent(EventType::DATAEVENT, pack, true);
        return;
    }
    // 找到冒号并提取值
    int colon = s.Find(":", posCL);
    if (colon == -1 || colon >= headerEnd) {
        // 无效的 Content-Length 行，视为协议错误
        WhenEvent(EventType::ERROREVENT, pack, "Invalid Content-Length header");
        return;
    }
    String after = s.Mid(colon + 1);
    int relLineEnd = after.Find("\r\n");
    int lineEnd;
    if (relLineEnd == -1 || colon + 1 + relLineEnd > headerEnd)
        lineEnd = headerEnd;
    else
        lineEnd = colon + 1 + relLineEnd;
    String val = s.Mid(colon + 1, lineEnd - (colon + 1));
    int len = ::StrInt(val);
    int totalNeeded = headerEnd + len;
    if (pack->get_wirte_num() > totalNeeded) { // 尾部存在粘包
		int tail_len = pack->get_wirte_num() - totalNeeded;// 计算尾部长度
        One<SockPacket> newpack = CreatCopySockPacket(pack);
        newpack->wirte(pack->offsetPtr(totalNeeded), tail_len);
		pack->ReduceToLeft(tail_len + totalNeeded);// 截断多余数据
        {
#ifdef PLATFORM_POSIX
            {
                Mutex::Lock __(piecepacks_mtx);
                piecepacks.Add(newpack->get_fd(), pick(newpack)); // 将尾部作为碎片缓存
            }
#else
			if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
            {
                Mutex::Lock __(piecepacks_mtx);
                piecepacks.Add(pack->get_fd(), pick(newpack)); // 没有接收完毕，放入碎片缓存
                One<SockPacket> revpack = CreatCopySockPacket(pack);
                WhenEvent(EventType::READEVENT, revpack, true);
            }
            else
            {
				WhenEvent(EventType::READEVENT, newpack, true);//直接通知上层继续接收
            }
#endif
        }
        WhenEvent(EventType::DATAEVENT, pack, true); // 抛出当前完整数据
        return;
    }
    if (pack->get_wirte_num() == totalNeeded) {
		// 刚好完整接收
		pack->ReduceToLeft(pack->get_free_space());// 截断多余空间
#ifdef PLATFORM_WIN32
        One<SockPacket> revpack = CreatCopySockPacket(pack);
        WhenEvent(EventType::READEVENT, revpack, true);//windows平台，需要投递一个新的数据接收
#endif
        WhenEvent(EventType::DATAEVENT, pack, -1);
        return;
    }
    // 小于需要的长度，属于碎片
    {
#ifdef PLATFORM_POSIX
        Mutex::Lock __(piecepacks_mtx);
        piecepacks.Add(pack->get_fd(), pick(pack));
#else
		if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
        {
            Mutex::Lock __(piecepacks_mtx);
			piecepacks.Add(pack->get_fd(), pick(pack));
            One<SockPacket> revpack = CreatCopySockPacket(pack);
            WhenEvent(EventType::READEVENT, revpack, true);
        }
        else
        {
            WhenEvent(EventType::READEVENT, pack, true);// Windows 平台直接通知上层继续接收
        }
#endif
    }
*/
}

void SocketFun::HandleIsHandshake(One<SockPacket> pack)
{
#ifdef PLATFORM_POSIX
	HandlePiecePack(pack);//首先执行一次碎片数据处理
#else
    if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
        HandlePiecePack(pack);
#endif
    if (pack->get_wirte_num() < sizeof(PacketHead)) // 不是完整的头部，属于碎片数据
    {
        {
#ifdef PLATFORM_POSIX
            Mutex::Lock __(piecepacks_mtx);
            piecepacks.Add(pack->get_fd(), pick(pack));
#else
            if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
            {
				Mutex::Lock __(piecepacks_mtx);
                One<SockPacket> revpack = CreatCopySockPacket(pack);
				piecepacks.Add(pack->get_fd(), pick(pack));
				WhenEvent(EventType::DATAEVENT, revpack, false);// 继续接收数据
            }
            else
            {
                if(sizeof(PacketHead) > pack->get_free_space())
					pack->addset_free_space(sizeof(PacketHead)); // 增加缓冲区继续接收数据
                WhenEvent(EventType::DATAEVENT, pack, false);// Windows 平台直接通知上层继续接收
            }
#endif
        }
        return;
    }
    const int headlen = sizeof(PacketHead);
    int payloadlen = 0;
    int masklen = 0;
    uint64_t datalen = 0;
    PacketFrame ptr; // 解析头部
    pack->OrderedCopy((char*)&ptr.frame.pack, headlen);
    if (ptr.frame.head.buf.OPCODE == (int)OPCODE::PONG || ptr.frame.head.buf.OPCODE == (int)OPCODE::PING)//心跳包直接丢弃
    {
		WhenEvent(EventType::PINGPONG, pack, "");// 心跳包事件
		return;
    }
    if (ptr.frame.head.buf.PAYLOAD <= 125) {
        datalen = ptr.frame.head.buf.PAYLOAD;
    } else if (ptr.frame.head.buf.PAYLOAD == 126) {
        payloadlen = 2;
        if (pack->get_wirte_num() < headlen + payloadlen) { // 扩展长度还未接收完
            {
#ifdef PLATFORM_POSIX
                Mutex::Lock __(piecepacks_mtx);
                piecepacks.Add(pack->get_fd(), pick(pack));
#else
				if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
                {
                    Mutex::Lock __(piecepacks_mtx);
                    One<SockPacket> revpack = CreatCopySockPacket(pack);
					piecepacks.Add(pack->get_fd(), pick(pack));
                    WhenEvent(EventType::DATAEVENT, revpack, false);// 继续接收数据
                }
                else
                {
                    if (pack->get_free_space() < payloadlen)
                        pack->addset_free_space(payloadlen); // 增加缓冲区继续接收数据
                    WhenEvent(EventType::DATAEVENT, pack, false);// Windows 平台直接通知上层继续接收
                }
#endif
            }
            return;
        }
		pack->OrderedCopy((char*)ptr.frame.pack.buf.PAYLOADLEN, sizeof(uint16_t));
        datalen = ntohs(*(uint16_t*)ptr.frame.pack.buf.PAYLOADLEN);
    } else if (ptr.frame.head.buf.PAYLOAD == 127) {
        payloadlen = 8;
        if (pack->get_wirte_num() < headlen + payloadlen) { // 扩展长度还未接收完
            {
#ifdef PLATFORM_POSIX
                Mutex::Lock __(piecepacks_mtx);
                piecepacks.Add(pack->get_fd(), pick(pack));
#else
				if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
                {
                    Mutex::Lock __(piecepacks_mtx);
                    One<SockPacket> revpack = CreatCopySockPacket(pack);
					piecepacks.Add(pack->get_fd(), pick(pack));
					WhenEvent(EventType::DATAEVENT, revpack, false);// 继续接收数据
                }
                else
                {
                    if (pack->get_free_space() < payloadlen)
                        pack->addset_free_space(payloadlen); // 增加缓冲区继续接收数据
                    WhenEvent(EventType::DATAEVENT, pack, false);// Windows 平台直接通知上层继续接收
                }
#endif
            }
            return;
        }
        pack->OrderedCopy((char*)ptr.frame.pack.buf.PAYLOADLEN, sizeof(uint64_t));
        datalen = ntohs(*(uint64_t *)ptr.frame.pack.buf.PAYLOADLEN);
    }
    if (ptr.frame.head.buf.MASK)
    {
        masklen = 4;
        if (pack->get_wirte_num() < headlen + payloadlen + masklen) // 掩码还未接收完
        {
            {
#ifdef PLATFORM_POSIX
                Mutex::Lock __(piecepacks_mtx);
				piecepacks.Add(pack->get_fd(), pick(pack));
#else
                if (pack->get_ssl())
                {
                    Mutex::Lock __(piecepacks_mtx);
                    One<SockPacket> revpack = CreatCopySockPacket(pack);
					piecepacks.Add(pack->get_fd(), pick(pack));
					WhenEvent(EventType::DATAEVENT, revpack, false);// 继续接收数据
                }
                else
                {
                    if (pack->get_free_space() < masklen)
                        pack->addset_free_space(masklen); // 增加缓冲区继续接收数据
                    WhenEvent(EventType::DATAEVENT, pack, false);// Windows 平台直接通知上层继续接收
                }
#endif
            }
            return;
        }
		pack->OrderedCopy((char*)&ptr.frame.pack.buf.MASKS, 4);
    }

    const uint64_t headerTotal = (uint64_t)headlen + (uint64_t)payloadlen + (uint64_t)masklen;

    // 如果未接收完一个完整帧，视为碎片
    if (pack->get_wirte_num() < (size_t)(headerTotal + datalen)) {
        {
#ifdef PLATFORM_POSIX
            Mutex::Lock __(piecepacks_mtx);
            piecepacks.Add(pack->get_fd(), pick(pack));
#else
			// Windows 平台
			if (pack->get_ssl())///存在ssl对象则说明是ssl连接，需要特殊处理
            {
				Mutex::Lock __(piecepacks_mtx);
                One<SockPacket> revpack = CreatCopySockPacket(pack);
				piecepacks.Add(pack->get_fd(), pick(pack));
				WhenEvent(EventType::DATAEVENT, revpack, false);// 继续接收数据
            }
            else
            {
                if (pack->get_free_space() < datalen)// 增加缓冲区继续接收数据
                    pack->addset_free_space(datalen); // 增加缓冲区继续接收数据
                WhenEvent(EventType::DATAEVENT, pack, false);// Windows 平台直接通知上层继续接收
            }
#endif
        }
        return;
    }

    // 处理粘包：如果超过一个帧，将多余数据切出为新的碎片包
    uint64_t totalFrameSize = headerTotal + datalen;
    if ((uint64_t)(pack->get_wirte_num()) > totalFrameSize) {
        uint64_t tail_len = (uint64_t)(pack->get_wirte_num()) - totalFrameSize;
        One<SockPacket> newpack = CreatCopySockPacket(pack);
        newpack->ApplyBuf((size_t)tail_len);
        newpack->wirte(pack->offsetPtr((size_t)totalFrameSize), (size_t)tail_len);
        // 原包裁剪为只保留前 totalFrameSize 字节
        pack->ReduceToLeft((size_t)tail_len);
        {
#ifdef PLATFORM_POSIX
            Mutex::Lock __(piecepacks_mtx);
            piecepacks.Add(newpack->get_fd(), pick(newpack));
#else
            if (pack->get_ssl())//存在ssl对象则说明是ssl连接，需要特殊处理
            {
                Mutex::Lock __(piecepacks_mtx);
                piecepacks.Add(newpack->get_fd(), pick(newpack));
                One<SockPacket> revpack = CreatCopySockPacket(pack);
				WhenEvent(EventType::DATAEVENT, revpack, false);// 继续接收数据
			}
            else
            {
                if(newpack->get_free_space() < sizeof(PacketHead))
					newpack->addset_free_space(sizeof(PacketHead));// 增加缓冲区继续接收数据
                WhenEvent(EventType::DATAEVENT, newpack, false);// Windows 平台直接通知上层继续接收
            }
#endif
        }
		HandleBlockPack(&ptr, pick(pack));// 处理当前完整帧
		return;
    }
    // 现在 pack 包含完整单帧数据，裁剪到头部末尾并处理
    if ((uint64_t)(pack->get_wirte_num()) == totalFrameSize) {
		pack->RemoveEmpty();// 截断多余空间
		HandleBlockPack(&ptr, pick(pack));// 处理当前完整帧
        return;
    }
    // 防御性处理：不应走到这里
    {
#ifdef PLATFORM_POSIX
        Mutex::Lock __(piecepacks_mtx);
        piecepacks.Add(pack->get_fd(), pick(pack));
#else
        if (pack->get_ssl())
        {
            Mutex::Lock __(piecepacks_mtx);
            One<SockPacket> revpack = CreatCopySockPacket(pack);
            piecepacks.Add(pack->get_fd(), pick(pack));
			WhenEvent(EventType::DATAEVENT, revpack, false);// 继续接收数据
        }
        else
        {
            WhenEvent(EventType::DATAEVENT, pack, false);// Windows 平台直接通知上层继续接收
        }
#endif
    }
}

void SocketFun::HandleReadEvent(One<SockPacket> pack)
{
#ifdef PLATFORM_POSIX
    // POSIX: read directly from socket into pack buffer
    while (true)
    {
        if (*pack <= 0)//防御性的，针对Linux平台，pack缓冲为0需要执行一次获取接收缓冲区大小
            MemoryFreeRet(pack);
        int n = recv(pack->get_fd(), pack->get_wirte_ptr(), pack->get_free_space(), MSG_DONTWAIT);
        if (n > 0)
        {
            pack->add_wirte_num((size_t)n);
            int bytes_available = 0;
            if (!NetFionread(pack->get_fd(), &bytes_available))
                bytes_available = 512;
            if (bytes_available > 0 && bytes_available > pack->get_free_space())
                pack->addset_free_space(bytes_available);
        }
        else if (n == 0) {
            WhenEvent(EventType::CLOSEDEVENT, pack, -1);
            return;
        }
        else {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                pack->ReduceToLeft(pack->get_free_space());// 截断多余空间
                break;
            }
            else {
                WhenEvent(EventType::ERROREVENT, pack, strerror(errno));
                return;
            }
        }
    }
#endif
    if (pack->GetPacketType() == PacketType::SSL_RECV && pack->get_ssl())
    {
        NetSSL* sslobj = pack->get_ssl();
        One<SockPacket> revpack = CreatCopySockPacket(pack);
        revpack->SetPacketType(PacketType::RECV);
        while (true)
        {
            if(pack->get_not_read_num() > 0)
            {
                int n = sslobj->BIOWrite(pack->get_read_ptr(), pack->get_not_read_num());
                if (n <= 0)
                    break;
                pack->add_read_num(n);
            }
            else
            {
                int hsret = sslobj->SSLDoHandshake();

                if (hsret == 0)
                {
                    // 握手未完成（WANT_READ / WANT_WRITE）。
                    // 此时 wbio 里已生成握手响应数据（ServerHello 等），
                    // 必须取出并回写给对端，否则对端永远收不到，握手死锁。
                    // 复好处：HandleSendEvent 的 SSL_SEND 分支在 pack 无数据时会
                    // 走 BIOPending -> BIORead -> SENDENDEVENT，正好把 wbio 排空发出去。
                    One<SockPacket> sndpack = CreatCopySockPacket(pack);
                    sndpack->SetPacketType(PacketType::SSL_SEND);
                    HandleSendEvent(pick(sndpack));
                    return;
                }
                if (hsret < 0)
                {
                    WhenEvent(EventType::ERROREVENT, revpack, "");
                    return;
                }
                if (sslobj->SSLPending() != -1)
                {
                    if (sslobj->SSLPending() > 0)
                    {
                        if (revpack->get_free_space() < sslobj->SSLPending())
                            revpack->addset_free_space(sslobj->SSLPending());
                        int n = sslobj->SSLRead(revpack->get_wirte_ptr(), revpack->get_free_space());
                        if (n > 0)
                            revpack->add_wirte_num(n);
                        if (n == -1)
                        {
                            WhenEvent(EventType::ERROREVENT, revpack, "");
                            return;
                        }
                    }
                    else
                    {
                        WhenEvent(EventType::DATAEVENT, revpack, false);
                        return;
                    }
                }
                else
                {
                    WhenEvent(EventType::ERROREVENT, revpack, "");
                    return;
                }
            }
        }
    }
    else
    {
        // Windows: read is posted via IOCP, so just hand to processing
        if (pack->IsHandshake())
            HandleIsHandshake(pick(pack));
        else
            HandleNoHandshake(pick(pack));
    }
}
void SocketFun::GetMask(uint8_t* mask)
{
	uint64 masktmp;
	::Random64(&masktmp,1);
	for(int i = 0;i<4;i++)
		mask[i] = ((uint8_t*)&masktmp)[i*2];
}
void SocketFun::HandleSendEvent(One<SockPacket> pack)
{
    if (pack->GetPacketType() == PacketType::SSL_SEND && pack->get_ssl())
    {
		NetSSL* sslobj = pack->get_ssl();
        One<SockPacket> revpack = CreatCopySockPacket(pack);
        revpack->SetPacketType(PacketType::SEND);
        if (stype == SockType::CLIENT && sslobj->SSLDoHandshake() != 1)
            return;
        while(true)
        {
            if (pack->get_not_read_num() > 0)
            {
                int n = sslobj->SSLWrite(pack->get_read_ptr(), (int)pack->get_not_read_num());
                if (n > 0)
                    pack->add_read_num((size_t)n);
                else
                {
                    if (n == -1)
                    {
                        WhenEvent(EventType::ERROREVENT, pack, "SSL write error");
                        return;
                    }
                }
            }
            else
            {
                if (sslobj->BIOPending() != -1)
                {
                    if (sslobj->BIOPending() > 0)
                    {
                        if(revpack->get_free_space() < sslobj->BIOPending())
                            revpack->addset_free_space(sslobj->BIOPending());
                        int n = sslobj->BIORead(revpack->get_wirte_ptr(), (int)revpack->get_free_space());
                        if (n > 0)
                            revpack->add_wirte_num(n);
                        if(n == -1)
                        {
                            WhenEvent(EventType::ERROREVENT, revpack, "");
                            return;
                        }
                    }
                    else
                    {
                        WhenEvent(EventType::SENDENDEVENT, revpack, false);
                        return;
                    }
                }
                else
                {
                    WhenEvent(EventType::ERROREVENT, pack, "");
                    return;
                }
            }
			
		}
    }
    else
    {
#if defined(PLATFORM_WIN32)
        if (pack->get_wirte_num() > 0 && pack->get_not_read_num() > 0)
            WhenEvent(EventType::SENDENDEVENT, pack, false);
        else
            WhenEvent(EventType::SENDENDEVENT, pack, true);
#else
        while (true)
        {
            int n = send(pack->get_fd(), pack->get_read_ptr(), pack->get_not_read_num(), MSG_DONTWAIT | MSG_NOSIGNAL);
            if (n > 0)
            {
                pack->add_read_num((size_t)n);
                if (pack->get_not_read_num() == 0)
                {
                    WhenEvent(EventType::SENDENDEVENT, pack, true);
                    return;
                }
            }
            else
            {
                if (n < 0 && errno == EAGAIN)
                {
                    WhenEvent(EventType::SENDCACHENULL, pack, true);
                    return;
                }
                else {
                    WhenEvent(EventType::ERROREVENT, pack, strerror(errno));
                    return;
                }
            }
        }
#endif
    }
}
void SocketFun::HandleQueueEvent(One<SockPacket>& pack)
{
	if(pack->GetPacketType() == PacketType::SEND || pack->GetPacketType() == PacketType::SSL_SEND)
		HandleSendEvent(pick(pack));
    else
    {
        if (pack->GetPacketType() == PacketType::RECV || pack->GetPacketType() == PacketType::SSL_RECV)
            HandleReadEvent(pick(pack));
    }
}
void NetLibrary::SocketFun::MemoryFreeRet(One<SockPacket>& pack, size_t size)
{
	Mutex::Lock __(_memret_mtx);
    if (pack->GetPacketType() == PacketType::RECV || pack->GetPacketType() == PacketType::SSL_RECV) {
        u_long bytes_available = pack->IsHandshake() ? sizeof(PacketHead) : 0;
#if defined(PLATFORM_WIN32)
        if (!bytes_available && ioctlsocket(pack->get_fd(), FIONREAD, &bytes_available) != 0) {
            pack->ApplyBuf(1024);
            int err = WSAGetLastError();
			assetEventBus.Emit(SYSEVENT::SYSERR, "ioctlsocket FIONREAD failed");
            return;
        }
#else
        if (!bytes_available && ioctl(pack->get_fd(), FIONREAD, &bytes_available) != 0) {
            pack->ApplyBuf(1024);
			int err = errno;
			assetEventBus.Emit(SYSEVENT::SYSERR, "ioctl FIONREAD failed");
            return;
        }
#endif
        if (bytes_available > 0)
            pack->ApplyBuf((size_t)bytes_available);
        else
            pack->ApplyBuf(sizeof(PacketHead));

        WhenEvent(EventType::MEMRETEVENT, pack, (int)size);
    }
}
// MemoryPool::whendeallocate expects a callback taking only size_t.
// This helper emits an event without requiring a SockPacket context.
void NetLibrary::SocketFun::MemoryFreeSize(size_t size)
{
    // No packet context available; notify upper layer that memory has been returned.
    // Use a dummy packet with invalid fd only if consumers expect a packet, otherwise
    // prefer a dedicated event carrying size only. Here we signal via WhenClientClosed
    // is not appropriate; keep it minimal and do nothing if WhenEvent requires a packet.
    IGNORE_RESULT(size);
}
PacketFrame SocketFun::HandleBuildHeader(One<SockPacket>& pack, OPCODE code, size_t dlen)
{
    PacketFrame frame;
    int offset = 0;
	frame.frame.head.buf.FIN = code == OPCODE::ISBLOCK ? 0 : 1;
    frame.frame.head.buf.OPCODE = (int)code;
    frame.frame.head.buf.MASK = stype == SockType::SERVER ? 0 : 1;
    if(dlen <= 125)
        frame.frame.head.buf.PAYLOAD = (uint8_t)dlen;
    if(dlen <= 0xFFFF)
    {
		offset += 2;
        frame.frame.head.buf.PAYLOAD = 126;
        uint16_t raw16 = htons((uint16_t)dlen);
		for (int i = 0; i < 2; i++)
			frame.frame.pack.buf.PAYLOADLEN[i] = ((uint8_t*)&raw16)[i];
    }
	if (dlen > 0xFFFF)
    {
		offset += 8;
        frame.frame.head.buf.PAYLOAD = 127;
		uint64_t raw64 = htonll((uint64_t)dlen);
		for (int i = 0; i < 8; i++)
			frame.frame.pack.buf.PAYLOADLEN[i] = ((uint8_t*)&raw64)[i];
    }
	if(frame.frame.head.buf.MASK)
	{
		offset += 4;
		uint8_t mask[4];
		GetMask(mask);
        for(int i = 0;i<4;i++)
			frame.frame.pack.buf.MASKS[i] = mask[i];
	}
	pack->wirte((char*)&frame, sizeof(PacketHead) + offset);//写入头部数据
	return frame;
}
void SocketFun::HandleBuildPack(One<SockPacket> pack, OPCODE code, char* d, int size)
{
	PacketFrame frame = HandleBuildHeader(pack, code, size);
	if(frame.frame.head.buf.MASK && d != NULL)
    {
        for (size_t i = 0; i < size; i++)
            d[i] ^= frame.frame.pack.buf.MASKS[i % 4];
        if(d)
            pack->wirte(d, size);
    }
#if defined(PLATFORM_WIN32)
	HandleQueueEvent(pack);
#else
	threadpool.SubmitTask({ this, &SocketFun::HandleQueueEvent },pick(pack),pack->get_fd());
#endif
}
void SocketFun::SubPacket(SOCKET fd, bool isHandshake, bool isBinary, const AssetLib::BinaryData& d, NetSSL* s)
{
    int sndbuf_size = 0;
#if defined(PLATFORM_WIN32)
    int optlen = sizeof(sndbuf_size);
    int rc = getsockopt(fd, SOL_SOCKET, SO_SNDBUF, (char*)&sndbuf_size, &optlen);
    if (rc == SOCKET_ERROR) {
        // fallback to a reasonable chunk size on error
        sndbuf_size = 8192;
    }
#else
    socklen_t optlen = sizeof(sndbuf_size);
    if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf_size, &optlen) < 0) {
        // fallback to a reasonable chunk size on error
        sndbuf_size = 8192;
    }
#endif

    if ((size_t)sndbuf_size >= d.size)
    {
        One<SockPacket> pack = new SockPacket(s, fd, s ? PacketType::SSL_SEND : PacketType::SEND, &mpool);
        pack->SetHandshake(isHandshake);
		pack->SetBinary(isBinary);
        if(isHandshake)
            HandleBuildPack(pick(pack), isBinary ? OPCODE::BINARY : OPCODE::TEXTONLY, (char*)d.data, d.size);
        else
        {
            pack->wirte(d);
			WhenEvent(EventType::SENDENDEVENT, pack, false);
        }
    }
    else
    {
        // compute number of chunks safely
        int chunk = sndbuf_size > 0 ? sndbuf_size : 8192;
        int parts = (int)(d.size / (size_t)chunk + (d.size % (size_t)chunk ? 1 : 0));
        for(int i = 0; i < parts; i++)
        {
            One<SockPacket> pack = new SockPacket(fd, s ? PacketType::SSL_SEND : PacketType::SEND, &mpool);
            pack->SetHandshake(isHandshake);
            pack->SetBinary(isBinary);
            if(isHandshake)
            {
                int this_len = (int)((i == parts - 1) ? (d.size - (size_t)i * chunk) : (size_t)chunk);
                OPCODE code = isBinary ? OPCODE::BINARY : OPCODE::TEXTONLY;
                HandleBuildPack(pick(pack), code, (char*)d.data + i * (size_t)chunk, this_len);
            }
            else
            {
                int this_len = (int)((i == parts - 1) ? (d.size - (size_t)i * chunk) : (size_t)chunk);
				pack->wirte((char*)d.data + i * (size_t)chunk, this_len);
                WhenEvent(EventType::SENDENDEVENT, pack, false);
            }
        }
    }
}

One<SockPacket> NetLibrary::SocketFun::CreatSockPacket(SOCKET fd, PacketType type, bool isHandshake, bool isBinary, NetSSL* s)
{
    One<SockPacket> pack = new SockPacket(s, fd, type, &mpool);
	pack->SetHandshake(isHandshake);
	pack->SetBinary(isBinary);
	return pick(pack);
}

One<SockPacket> NetLibrary::SocketFun::CreatCopySockPacket(One<SockPacket>& pack)
{
	One<SockPacket> npack = new SockPacket(pack->get_ssl(), pack->get_fd(), pack->GetPacketType(), &mpool);
	npack->SetHandshake(pack->IsHandshake());
	npack->SetBinary(pack->IsBinary());
    return pick(npack);
}

void NetLibrary::SocketFun::SubmitWork(One<SockPacket> pack, bool isSignal, bool isHead)
{
#ifdef PLATFORM_WIN32
	HandleQueueEvent(pack);
#else
	threadpool.SubmitTask({ this, &SocketFun::HandleQueueEvent }, pick(pack), pack->get_fd(), isSignal, isHead);
#endif
}

void NetLibrary::SocketFun::SubmitWork(UserInfo* u, OPCODE code, AssetLib::BinaryData d)
{
	One<SockPacket> pack = new SockPacket(u->GetSSL_Ptr(), u->GetFd(), u->GetSSL() ? PacketType::SSL_SEND : PacketType::SEND, &mpool);
    pack->SetHandshake(!(u->GetProtocol() == ProtocolType::CONNECT));
	pack->SetBinary(code == OPCODE::BINARY ? true : false);
	HandleBuildPack(pick(pack), code, (char*)d.data, d.size);
}

void NetLibrary::SocketFun::SubmitWork(UserInfo* u, AssetLib::BinaryData d, bool isBinary, bool issend)
{
    if (!u)
        return;
    if (issend)
    {
#if defined(PLATFORM_POSIX)
        if (!d)
        {
            threadpool.SubmitTask({ this, &SocketFun::HandleQueueEvent }, NULL, u->GetFd());
            return;
        }
#endif
        SubPacket(u->GetFd(), !(u->GetProtocol() == ProtocolType::CONNECT), isBinary, d, u->GetSSL_Ptr());
        return;
    }
    One<SockPacket> pack = new SockPacket(u->GetSSL_Ptr(), u->GetFd(), u->GetSSL() ? PacketType::SSL_RECV : PacketType::RECV, &mpool);
    pack->SetHandshake(!(u->GetProtocol() == ProtocolType::CONNECT));
    pack->SetBinary(isBinary);
#if defined(_WIN32)
    u_long bytes_available = 0;
    if (ioctlsocket(u->GetFd(), FIONREAD, &bytes_available) != SOCKET_ERROR)
    {
        if (bytes_available > 0)
            pack->ApplyBuf((size_t)bytes_available);
        else
			pack->ApplyBuf(sizeof(PacketHead));
    }
#endif
    WhenEvent(EventType::DATAEVENT, pack, false);
}

void SocketFun::SetMemorySize(size_t size)
{
    if(mpool.IsNull())
        mpool.initialize(size);
    // Bind to size-only callback to satisfy Event<size_t> signature
    mpool.SetDeallocateMemberCallback(this, &SocketFun::MemoryFreeSize);
}

bool NetLibrary::SocketFun::IsAnyMem(size_t size)
{
	return mpool.IsAny(size);
}

void NetLibrary::SocketFun::ShutDownFn()
{
	threadpool.StopAll();
	piecepacks.Clear();
	blockpacks.Clear();
	mpool.release();
}





