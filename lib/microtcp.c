/*
 * microtcp, a lightweight implementation of TCP for teaching,
 * and academic purposes.
 *
 * Copyright (C) 2015-2017  Manolis Surligas <surligas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "microtcp.h"
#include "../utils/crc32.h"

microtcp_sock_t
microtcp_socket (int domain, int type, int protocol)
{
  microtcp_sock_t new_sckt;
  new_sckt.state = UNKNOWN;
  new_sckt.sd = socket(domain,type,IPPROTO_UDP);
  if(new_sckt.sd ==-1){
    perror("Socket bind failed\n");
    new_sckt.state = INVALID;
    return new_sckt;
  }
  
  new_sckt.init_win_size=MICROTCP_WIN_SIZE;
  new_sckt.curr_win_size=MICROTCP_WIN_SIZE;

  //We have to set a threshold and cwnd
  new_sckt.cwnd=MICROTCP_INIT_CWND;
  new_sckt.ssthresh=MICROTCP_INIT_SSTHRESH;
  
  return new_sckt;

}

int
microtcp_bind (microtcp_sock_t *socket, const struct sockaddr *address,
               socklen_t address_len)
{
  
  int bind_sckt = bind(socket->sd,address,address_len);
  if(bind_sckt==-1){
    perror("TCP bind");
    socket->state = INVALID;
    return bind_sckt;
  }
  socket->state = BIND;
  return bind_sckt;
  

}

int
microtcp_connect (microtcp_sock_t *socket, const struct sockaddr *address,
                  socklen_t address_len)
{
	microtcp_header_t sender_header;
	microtcp_header_t receiver_header;

	uint16_t check_checksum;
	uint16_t temp_checksum;
	uint8_t buf[MICROTCP_RECVBUF_LEN];

	int i;

	receiver_header.future_use0=0;
	receiver_header.future_use1=0;
	receiver_header.future_use2=0;

	sender_header.future_use0= 0;
	sender_header.future_use1= 0;
	sender_header.future_use2= 0;

	srand(time(0));

	//Start the handshake 
	sender_header.seq_number = htonl(rand()%600); //N
	sender_header.ack_number = 0;
	sender_header.control = htons(SYN);
	sender_header.data_len = 0;
	sender_header.window = 0;
	sender_header.checksum = 0;

	//Make our checksum field
	for(i = 0; i < MICROTCP_RECVBUF_LEN; i++) buf[i] = 0;
	memcpy(buf, &sender_header, sizeof(microtcp_header_t));
	sender_header.checksum = htonl(crc32(buf, sizeof(buf)));


	//Sending the first packet
	if(sendto(socket->sd,(void*)&sender_header,sizeof(microtcp_header_t),0,address,address_len) ==-1){
		socket->state = INVALID;
		perror("Sender was unable to send SYN packet");
		return -1;
	}

	//Waiting to receive reply
	if(recvfrom(socket->sd,(void*)&receiver_header,sizeof(microtcp_header_t),0,(struct sockaddr*)address,&address_len) ==-1){
		socket->state=INVALID;
		perror("Receiver was unable to send 'SYN ACK' packet");
		return -1;
	}


	temp_checksum = ntohl(receiver_header.checksum);

	receiver_header.checksum = 0;
	for(i = 0; i < MICROTCP_RECVBUF_LEN; i++) buf[i] = 0;
	memcpy(buf, &receiver_header, sizeof(microtcp_header_t));
	check_checksum = crc32(buf, sizeof(buf));

	//Check if checksum is correct
	if(check_checksum != temp_checksum){
		socket->state = INVALID;
		perror("Error in checksum 2nd packet connect");
		return -1;
	}


	//check if 2nd handshake was a SYN ACK
	receiver_header.control = ntohs(receiver_header.control);
	if(receiver_header.control!=SYN_ACK){
		socket->state=INVALID;
		perror("Error because 2nd packet wasn't SYN,ACK");
		return -1;
	}
	
	
	//Get window size from receiver
	//socket->curr_win_size = ntohs(receiver_header.window);

	//now set sender's data accordingly from receiver
	sender_header.ack_number = htonl(receiver_header.seq_number); 
	sender_header.seq_number = htonl(receiver_header.ack_number);
	sender_header.ack_number++;
	sender_header.control    = htons(ACK);
	sender_header.window     = htons(socket->curr_win_size);
	sender_header.data_len   = 0;
	sender_header.checksum   = 0;

	//Make our checksum field
	for(i = 0; i < MICROTCP_RECVBUF_LEN; i++) buf[i] = 0;
	memcpy(buf, &sender_header, sizeof(microtcp_header_t));
	sender_header.checksum = htonl(crc32(buf, sizeof(buf)));


	//final packet of the 3way handshake:
	//tmp_send is used before so if I need to keep variable value, change it.
	if(	sendto(socket->sd,(void*)&sender_header,sizeof(microtcp_header_t),0,address,address_len) == -1){
		socket->state = INVALID;
		perror("There was an error while sending 3rd packet.");
		return -1;
	}

	//If it reaches this point it means that there wasn't any error
	//and the handshake was confirmed 
	socket->state=ESTABLISHED;
	socket->seq_number = ntohl(sender_header.seq_number);
	socket->ack_number = ntohl(sender_header.ack_number);
	return 1;
}

int
microtcp_accept (microtcp_sock_t *socket, struct sockaddr *address,
                 socklen_t address_len)
{
	uint16_t check_checksum;
	uint16_t temp_checksum;
	uint8_t buf[MICROTCP_RECVBUF_LEN];			

	microtcp_header_t sender_header;
	microtcp_header_t receiver_header;
	
	int i;
	srand(time(0));

	receiver_header.future_use0=0;
	receiver_header.future_use1=0;
	receiver_header.future_use2=0;

	sender_header.future_use0= 0;
	sender_header.future_use1= 0;
	sender_header.future_use2= 0;

	//use recvfrom method to check 1st packet of the handshake:	
	if(recvfrom(socket->sd,(void*)&receiver_header,sizeof(microtcp_header_t),0,address,&address_len) == -1){
		perror("Failed to accept the 1st packet");
		socket->state = INVALID;
		return -1;
	}
	
	//Keep receivers checksum so we can check it
	temp_checksum = ntohl(receiver_header.checksum);
	receiver_header.checksum = 0;

	//Make our checksum field
	for(i = 0; i < MICROTCP_RECVBUF_LEN; i++) buf[i] = 0;
	memcpy(buf, &receiver_header, sizeof(microtcp_header_t));
	check_checksum = crc32(buf, sizeof(buf));

	//Check if checksum is correct
	if(check_checksum != temp_checksum){
		socket->state = INVALID;
		perror("Error in checksum 2nd packet accept");
		return -1;
	}


	//Check if the 1st packet was a SYN
	if(ntohs(receiver_header.control)!=SYN){
		perror("The first packet wasn't a SYN");
		socket->state = INVALID;
		return -1;
	}


	//makea random seq (M) for sending back packet
	sender_header.seq_number = htonl(rand()%800);		//M
	sender_header.ack_number = htonl(ntohl(receiver_header.seq_number) + 1); //ack = recvACK + !
	sender_header.control    = htons(SYN_ACK);
	sender_header.window     = 0; //Not sure about this.
	sender_header.data_len   = 0;
	sender_header.checksum   = 0;
	
	for(i = 0; i < MICROTCP_RECVBUF_LEN; i++) buf[i] = 0;
	memcpy(buf, &sender_header, sizeof(microtcp_header_t));
	sender_header.checksum = htonl(crc32(buf, sizeof(buf)));

	//init socket window size
	socket->init_win_size = MICROTCP_WIN_SIZE;
	

	//send the packet back to the host (2nd)
	if(sendto(socket->sd,(void*)&sender_header,sizeof(microtcp_header_t),0,address,address_len) == -1){
		socket->state = INVALID;
		perror("Error while sending 2nd packet back");
		return -1;
	}


	//Now we have to wait for the 3rd packet to arrive from the host
	if(recvfrom(socket->sd,(void*)&receiver_header,sizeof(microtcp_header_t),0,address,&address_len) ==-1){
		socket->state = INVALID;
		perror("Error while geting 3rd packet");
		return -1;
	}


	//Keep receivers checksum so we can check it
	temp_checksum = ntohl(receiver_header.checksum);
	receiver_header.checksum = 0;

	//Make our checksum field
	for(i = 0; i < MICROTCP_RECVBUF_LEN; i++) buf[i] = 0;
	memcpy(buf, &receiver_header, sizeof(microtcp_header_t));
	check_checksum = crc32(buf, sizeof(buf));

	//Check if checksum is correct
	if(check_checksum != temp_checksum){
		socket->state = INVALID;
		perror("Error in checksum 2nd packet accept");
		return -1;
	}

	if(ntohs(receiver_header.control) != ACK){
		socket->state = INVALID;
		perror("3rd packet is not a ACK");
		return -1;
	}

	//Should be: Host_send seq = M, ack= N | Rcv seq= N, ack= M+1

	if(ntohl(sender_header.seq_number)+ 1 == receiver_header.ack_number){
		//means we did succesfully handshake
		socket->state = ESTABLISHED;
		socket->ack_number = ntohl(receiver_header.seq_number);
		socket->seq_number = ntohl(receiver_header.ack_number)+1;
		printf("Server: Three way handshake is successfull! \n");
		return 1;
	}else{
		perror("Invalid seq or ack in 3rd packet \n");
		socket->state = INVALID;
		return -1;
	}
  
}

int
microtcp_shutdown (microtcp_sock_t *socket, int how){

	uint16_t check_checksum;
	uint16_t temp_checksum;
	uint8_t buf[MICROTCP_RECVBUF_LEN];			

	microtcp_header_t sender_header;
	microtcp_header_t receiver_header;
  
  	int i;

	receiver_header.future_use0=0;
	receiver_header.future_use1=0;
	receiver_header.future_use2=0;

	sender_header.future_use0=0;
	sender_header.future_use1=0;
	sender_header.future_use2=0;


	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = MICROTCP_ACK_TIMEOUT_US;

	if(socket->state==CLOSING_BY_PEER){  //SERVER
	
		//We wait to receive first packet FIN,ACK, seq = X
		printf("SERVER ENTERED SHUTDOWN \n");

		//Send 2nd packet  ACK, ack = x + 1
		sender_header.seq_number=0;
		sender_header.ack_number=htonl(socket->seq_number + 1);	
		sender_header.control=htons(ACK);
		sender_header.window=htons(socket->curr_win_size);
		sender_header.data_len=0;
		sender_header.checksum=0;


		//Checksum
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++){
			buf[i]=0;
		}
		memcpy(buf,&sender_header,sizeof(sender_header));
		temp_checksum=crc32(buf,sizeof(buf));

		if (sendto(socket->sd,(void *)&sender_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len) == -1){
			socket->state=INVALID;
			perror("Failed to send second packet \n");
			return -1;
		}

		//Send 3rd packet FIN, ACK, seq = Y
		sender_header.seq_number=htonl(socket->seq_number);
		sender_header.control=htons(FIN + ACK);
		sender_header.window=htons(socket->curr_win_size);
		sender_header.data_len=0;
		sender_header.checksum=0;

		//checksum
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++){
			buf[i]=0;
		}
		memcpy(buf,&sender_header,sizeof(sender_header));
		temp_checksum=crc32(buf,sizeof(buf));
		sender_header.checksum=htonl(temp_checksum);

		if (sendto(socket->sd,(void *)&sender_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len) == -1){
			socket->state=INVALID;
			perror("Failed to send 3rd packet \n");
			return -1;
		}


		//Now we wait to receive the 4th packet
		if(recvfrom(socket->sd,(void*)&receiver_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,&socket->address_len) == -1){
			//error at waiting for a connection
			perror("Failed to receive 4th packet \n");
			socket->state = INVALID;
      		return -1;
		}

		//Check packet received (Checksums)
		temp_checksum=ntohl(receiver_header.checksum);
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++){
				buf[i]=0;
		}
		memcpy(buf,&receiver_header,sizeof(microtcp_header_t));
		/*check_checksum=crc32(buf,sizeof(buf));					
		if(check_checksum!=temp_checksum){	//error on checksum
			socket->state=INVALID;
			perror("Error with checksum while receiving 4th packet");
			return -1;
		}*/

		receiver_header.control = ntohs(receiver_header.control);
		if(receiver_header.control!=ACK){
			socket->state=INVALID;
			perror("Error because 4th packet wasn't ACK \n");
			return -1;
		}

		if(receiver_header.ack_number != sender_header.seq_number + 1){
			socket->state=INVALID;
			perror("Error because ack sent is not correct 4th packet \n");
			return -1;
		}	

	}else{																//CLIENT

		printf("CLIENT ENTERED SHUTDOWN \n");
		//Now we create the 1st FINACK packet for sending
		sender_header.seq_number=htonl(socket->seq_number);	//X
		sender_header.ack_number=0;
		sender_header.control=htons(FIN + ACK);
		sender_header.window=htons(socket->curr_win_size);
		sender_header.data_len=0;
		sender_header.checksum=0;

		

    	//Time to  create checksum
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++){
        	buf[i]=0;
   	 	}	
		memcpy(buf,&sender_header,sizeof(sender_header));
		temp_checksum=crc32(buf,sizeof(buf));
		sender_header.checksum=htonl(temp_checksum);
		
    
		if (sendto(socket->sd,(void *)&sender_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len) == -1){
			socket->state=INVALID;
			perror("Failed to send first packet \n");
			return -1;
		}


		//Now we wait to receive the 2nd packet
		if(recvfrom(socket->sd,(void*)&receiver_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,&socket->address_len) == -1){
			//error at waiting for a connection
			perror("Failed to receive second packet \n");
      		socket->state = INVALID;
      		return -1;
		}
		

		
		temp_checksum = ntohl(receiver_header.checksum);
		receiver_header.checksum = 0;
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++){
			buf[i]=0;
   		}		
		memcpy(buf, &receiver_header, sizeof(receiver_header));
		check_checksum=crc32(buf, sizeof(buf));

		/*printf("%d, %d \n",temp_checksum,check_checksum);
		if(check_checksum != temp_checksum){	
			perror("Error with checksum while receiving 2nd packet");
      		socket->state=INVALID;
			return-1;
		}*/

		

		receiver_header.control = ntohs(receiver_header.control);
		if(receiver_header.control!=ACK){
			socket->state=INVALID;
			perror("Error because 2nd packet wasn't ACK");
			return -1;
		}

		/*if(sender_header.seq_number + 1 != receiver_header.ack_number){
			socket->state=INVALID;
			perror("Incorrect seq received from packet 2");
			return -1;
		}*/

    	socket->state = CLOSING_BY_HOST;
		sender_header.seq_number = receiver_header.ack_number;

		//Now we wait to receive the 3rd packet
		if(recvfrom(socket->sd,(void*)&receiver_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,&socket->address_len) == -1){
			//error at waiting for a connection
			perror("Failed to receive third packet \n");
      		socket->state = INVALID;
			return -1;
		}
		

		temp_checksum = ntohl(receiver_header.checksum);
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++){
			buf[i]=0;
		}
		memcpy(buf,&receiver_header,sizeof(receiver_header));
		check_checksum=crc32(buf,sizeof(buf));

		/*if(check_checksum!=temp_checksum){	
			socket->state=INVALID;
			perror("Error with checksum while receiving 3rd packet");
			return -1;
		}*/


		receiver_header.control = ntohs(receiver_header.control);
		if(receiver_header.control!=FIN + ACK){
			socket->state=INVALID;
			perror("Error because 3rd packet wasn't ACK");
			return -1;
		}

		//Send 4th packet ACK, seq = X+1, ack Y+1
		sender_header.ack_number=receiver_header.seq_number + 1;	//Y+1
		sender_header.control=htons(ACK);
		sender_header.window=htons(socket->curr_win_size);
		sender_header.data_len=0;
		sender_header.checksum=0;

		//Checksum
		for(i=0;i<MICROTCP_RECVBUF_LEN;i++){
			buf[i]=0;
		}	
		memcpy(buf,&sender_header,sizeof(sender_header));
		temp_checksum=crc32(buf,sizeof(buf));
		sender_header.checksum=htonl(temp_checksum);

		if (sendto(socket->sd,(void *)&sender_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len) == -1){
			socket->state=INVALID;
			perror("Failed to send 4th packet \n");
      		return -1;
		}

	}
	
	printf("Shutdown completed: state = closed \n");
	socket->state=CLOSED;
	
	close(socket->sd);
	return 1;
}



ssize_t
microtcp_recv (microtcp_sock_t *socket, void *buffer, size_t length, int flags){

	int tmp_rcv,tmp_send;
				
	microtcp_header_t sender_header;
	microtcp_header_t *receiver_header = (microtcp_header_t*)malloc(sizeof(microtcp_header_t));
	microtcp_header_t new_header_info;

	void *buf_for_send=malloc(sizeof(sender_header));
	void *buf_for_recv=malloc(MICROTCP_MSS);
	void *recv_buf=malloc(MICROTCP_RECVBUF_LEN);
	int   buf_index =0;
	int   recv_buf_index =0;
	void* host_data;

	ssize_t total_data_received=0;
	size_t send_seq_num = socket->ack_number;
	size_t recv_seq_num = socket->seq_number;
	
	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = MICROTCP_ACK_TIMEOUT_US;


	if(setsockopt(socket->sd , SOL_SOCKET ,SO_RCVTIMEO , &tv, sizeof(struct timeval)) < 0) {
		socket->state = INVALID;
		perror("Set Timeout\n");
		return -1;
	}

	tmp_rcv = recvfrom(socket->sd,buf_for_recv,MICROTCP_MSS,0,(struct sockaddr *)&socket->address,&socket->address_len);

	if(tmp_rcv ==-1){
		perror("TCP connection failed\n");
		//we send an ACK back
		sender_header.seq_number=0;
		sender_header.window=htons(MICROTCP_RECVBUF_LEN - recv_buf_index);
		sender_header.control=htons(ACK);
		sender_header.seq_number=htonl(send_seq_num++);
		sender_header.checksum=0;
		sender_header.data_len=0;
		
		tmp_send = sendto(socket->sd,(void *)&sender_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len);
		if(tmp_send ==-1){
			perror("TCP receive connection failed, ACK wasn't send\n");
			socket->state= INVALID;
			return -1;
		}
	}else{
		//Means we received packet
		receiver_header = (microtcp_header_t *)buf_for_recv;

		//check if control is FIN ACK for shutdown
		if(ntohs(receiver_header->control) == (FIN+ACK)){
			printf("FIN ACK DETECTED \n");
			socket->state = CLOSING_BY_PEER;  
			socket->seq_number = ntohl(receiver_header->seq_number);
			if(recv_buf_index!=0){
				memcpy(buffer+buf_index,recv_buf,recv_buf_index);
			} 
			return total_data_received;
		}

		receiver_header->data_len = ntohl(receiver_header->data_len);
		host_data = buf_for_recv + sizeof(microtcp_header_t);
		memcpy(recv_buf + recv_buf_index, host_data, receiver_header->data_len);
		
		//Update buffers
		total_data_received = total_data_received + receiver_header->data_len;
		recv_buf_index = recv_buf_index + receiver_header->data_len;
		recv_seq_num++;
		
		//for the next ACK packet:
		sender_header.seq_number = ntohl(send_seq_num);
		sender_header.ack_number = ntohl(recv_seq_num);
		sender_header.control    = htons(ACK);
		sender_header.window     = htons(MICROTCP_RECVBUF_LEN - recv_buf_index);
		sender_header.data_len   = receiver_header->data_len;
		sender_header.checksum   = 0;

		tmp_send = sendto(socket->sd,(void *)&sender_header,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,socket->address_len);

		if(tmp_send == -1){
			socket->state = INVALID;
			perror("Error while sending ACK\n");
			return -1;
		}

	}

    if(recv_buf_index){
        memcpy(buffer+buf_index,recv_buf,recv_buf_index);
    }

    socket->seq_number=recv_seq_num;
    return total_data_received;
}



ssize_t
microtcp_send (microtcp_sock_t *socket, const void *buffer, size_t length,
               int flags)
{
	
	size_t remaining;
	size_t data_len;
	size_t data_sent;

	size_t bytes_to_send;
	size_t chunks;
	size_t seq_number;
	size_t ack_number;
	size_t chunk_size;
	size_t duplicate_ack = 0;
	size_t prev_ack = -1;

	size_t error_retransmit;
	microtcp_header_t sender;
	microtcp_header_t *receiver = (microtcp_header_t *)malloc(sizeof(microtcp_header_t));
	
	struct Ack_Queue* queue;

	size_t i;

    struct timeval timeout;      
    timeout.tv_sec = 0;
    timeout.tv_usec = MICROTCP_ACK_TIMEOUT_US;

	seq_number = socket->seq_number;
	ack_number = socket->ack_number;
	data_sent = 0;
	data_len = 0;
	remaining = length;

	void *sender_buffer;

	while(data_sent < length){
		
		queue = createQueue(10000);
		error_retransmit = 0;

		bytes_to_send = min(socket->curr_win_size, remaining, socket->cwnd);
		chunks = bytes_to_send/MICROTCP_MSS;
		sender_buffer = malloc(MICROTCP_MSS);

		for(i = 0; i < chunks; i++){
			seq_number++;										//We increase seq = seq + 1 not seq = seq + data because it will be easier to debug
			enqueue(queue,seq_number + 1);

			sender.seq_number = htonl(seq_number);
			sender.ack_number = htonl(ack_number);
			sender.control = 0;
			sender.window = 0;
			sender.data_len = htonl(MICROTCP_MSS - sizeof(microtcp_header_t));
			sender.checksum = 0;

			assert(sender.data_len != 0);
		
			//Copy the data we will send
			memcpy(sender_buffer, &sender, sizeof(microtcp_header_t));
			memcpy(sender_buffer +sizeof(microtcp_header_t), buffer +data_sent, MICROTCP_MSS -sizeof(microtcp_header_t));
			

			//Send the chunk
			if (sendto(socket->sd,sender_buffer,MICROTCP_MSS,0,(struct sockaddr *)&socket->address,socket->address_len) < 0){
				socket->state=INVALID;
				perror("Microtcp_send failure! send failed");
				free(sender_buffer);
				return -1;
			}
			data_sent = data_sent + MICROTCP_MSS - sizeof(microtcp_header_t);	
		}

		free(sender_buffer);
		
		//Check if there is a semi -filled chunk
		if(bytes_to_send % MICROTCP_MSS){				
			seq_number++;										//We increase seq = seq + 1 not seq = seq + data because it will be easier to debug
			enqueue(queue,seq_number + 1);
			
			sender.seq_number = htonl(seq_number);
			sender.ack_number = htonl(ack_number);

			sender.control = 0;
			sender.window = 0;
			sender.checksum = 0;
			chunk_size = bytes_to_send%(MICROTCP_MSS-sizeof(microtcp_header_t));
			sender.data_len = htonl(chunk_size);
			
			assert(sender.data_len != 0);
			sender_buffer = malloc(MICROTCP_MSS);

			assert(sender_buffer);
			memcpy(sender_buffer, &sender, sizeof(microtcp_header_t));
			memcpy(sender_buffer +sizeof(microtcp_header_t), buffer +data_sent, chunk_size);

			if (sendto(socket->sd,sender_buffer,MICROTCP_MSS,0,(struct sockaddr *)&socket->address,socket->address_len) < 0){
				socket->state=INVALID;
				perror("Microtcp_send failure! send failed");
				free(sender_buffer);
				return -1;
			}

			data_sent = data_sent + chunk_size;
			free(sender_buffer);
			chunks++;
		}



		//Get the ACKs
		for(i = 0; i < chunks; i++){

			//TIMER IS 
			/*if(setsockopt(socket->sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(&timeout)) < 0){
				perror("Microtcp_send failure set timeout? \n");
				socket->state=INVALID;
				return -1;
			}*/
			
			if(recvfrom(socket->sd,receiver,sizeof(microtcp_header_t),0,(struct sockaddr *)&socket->address,&socket->address_len) == -1){
				perror("Microtcp_send failure! receive failed timeout 100% must retransmit! \n");
				error_retransmit = -2;
				Free_Ack_Queue(queue);
				break;
			}

			if(ntohs(receiver->control) != ACK){
				perror("Microtcp_send WARNING! Packet that was received was not ACK!");
				//WARNING NO ACK RECEIVED? HOW? 
				i--;
				continue;
			}	

			socket->curr_win_size = ntohs(receiver->window);
			// CHECK IF ITS EQUAL TO PREV ACK IF IT IS UPDATE COUNTER 
			if(receiver->ack_number != prev_ack){
				prev_ack = receiver->ack_number;
				if(prev_ack == queue->array[queue->front]){
					duplicate_ack = 0;
					error_retransmit = 0;
					if(socket->cwnd <= socket->ssthresh) socket->cwnd = socket->cwnd + MICROTCP_MSS;
					else socket->cwnd = socket->cwnd + 1;
					dequeue(queue);
				}
			}
			else{
				duplicate_ack++;
				if(duplicate_ack == 3 ){
					duplicate_ack = 0;
					error_retransmit = -1;
					break;
				}
				i--;
			}
		}


		switch (error_retransmit)
		{
			case -1: //triple duplicate
				printf("TRIPLE DUPLICATE \n");
				socket->ssthresh = socket->cwnd/2;
				socket->cwnd = socket->ssthresh;

				if(socket->cwnd == 0) socket->cwnd = 1;
			
				break;
			case -2: //timeout
				printf("TIMEOUT DETECTED \n");
				socket->ssthresh = socket->cwnd/2;
				socket->cwnd = 1;

				break;
			default: //everything good :)
				socket->seq_number=seq_number;
				//Free_Ack_Queue(queue);
				break;
		}


		socket->seq_number=seq_number;
		return data_sent;
	}
}

/* OUR FUNCTIONS */
struct Ack_Queue* createQueue(size_t capacity){
    struct Ack_Queue* queue = (struct Ack_Queue*)malloc(sizeof(struct Ack_Queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;

    queue->rear = capacity - 1;
    queue->array = (size_t*)malloc(queue->capacity * sizeof(size_t));
    return queue;
}


int isFull(struct Ack_Queue* queue){
    return (queue->size == queue->capacity);
}

int isEmpty(struct Ack_Queue* queue){
    return (queue->size == 0);
}


void enqueue(struct Ack_Queue* queue, int item){
	//assert(isFull(queue));
    queue->rear = (queue->rear + 1)% queue->capacity;
    queue->array[queue->rear] = item;
    queue->size = queue->size + 1;
}


size_t dequeue(struct Ack_Queue* queue){
	assert(isEmpty(queue));
    size_t ack = queue->array[queue->front];
    queue->front = (queue->front + 1)% queue->capacity;
    queue->size = queue->size - 1;
    return ack;
}

void Free_Ack_Queue(struct Ack_Queue* queue){

	while(!isEmpty(queue)){
		dequeue(queue);
	}
	free(queue->array);
	free(queue);

}

size_t min(size_t a, size_t b, size_t c){

	if(a < b){
		if(a < c) return a;
		else return c;
	}
	else{
		if(b < c) return b;
		else return  c;
	}
}