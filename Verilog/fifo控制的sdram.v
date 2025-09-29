//读写FIFO控制器

module  fifo_ctrl
(
    input   wire            sdram_clk		,   //sdram时钟
    input   wire            sdram_rst_n		,   //sdram复位信号
//写fifo信号
    input   wire            wr_fifo_wr_clk  ,   //写FIFO写时钟
    input   wire            wr_fifo_rst		,   //写复位信号	
    input   wire            wr_fifo_wr_req  ,   //写FIFO写请求
    input   wire    [15:0]  wr_fifo_wr_data ,   //写FIFO写数据
    input   wire    [23:0]  sdram_wr_b_addr ,   //写SDRAM首地址
    input   wire    [23:0]  sdram_wr_e_addr ,   //写SDRAM末地址
    input   wire    [9:0]   wr_burst_len    ,   //写SDRAM数据突发长度
    output  wire    [9:0]   wr_fifo_num     ,   //写fifo中的数据量
//读fifo信号
    input   wire            rd_fifo_rd_clk  ,   //读FIFO读时钟
    input   wire            rd_fifo_rd_req  ,   //读FIFO读请求
    input   wire    [23:0]  sdram_rd_b_addr ,   //读SDRAM首地址
    input   wire    [23:0]  sdram_rd_e_addr ,   //读SDRAM末地址
    input   wire    [9:0]   rd_burst_len    ,   //读SDRAM数据突发长度
    input   wire            rd_fifo_rst		,   //读复位信号
    output  wire    [15:0]  rd_fifo_rd_data ,   //读FIFO读数据
    output  wire    [9:0]   rd_fifo_num     ,   //读fifo中的数据量

    input   wire            read_valid      ,   //SDRAM读使能
    input   wire            init_end        ,   //SDRAM初始化完成标志
//SDRAM写信号
    input   wire            sdram_wr_ack    ,   //SDRAM写响应
    output  reg             sdram_wr_req    ,   //SDRAM写请求
    output  reg     [23:0]  sdram_wr_addr   ,   //SDRAM写地址
    output  wire    [15:0]  sdram_data_in   ,   //写入SDRAM的数据
//SDRAM读信号
    input   wire            sdram_rd_ack    ,   //SDRAM响应
    input   wire    [15:0]  sdram_data_out  ,   //读出SDRAM数据
    output  reg             sdram_rd_req    ,   //SDRAM读请求
    output  reg     [23:0]  sdram_rd_addr       //SDRAM读地址
);



wire	sdram_wr_ack_fall ;   //写响应信号下降沿
wire	sdram_rd_ack_fall ;   //读响应信号下降沿

reg		sdram_wr_ack_d1	;     //写响应打1拍
reg		sdram_wr_ack_d2	;     //写响应打2拍
reg		sdram_rd_ack_d1	;     //读响应打1拍
reg		sdram_rd_ack_d2	;     //读响应打2拍

//写响应信号打拍同步，降低亚稳态
always@(posedge sdram_clk or negedge sdram_rst_n)begin
    if(!sdram_rst_n)begin
        sdram_wr_ack_d1 <=  1'b0;
        sdram_wr_ack_d2 <=  1'b0;	
	end
    else begin
        sdram_wr_ack_d1 <=  sdram_wr_ack;
		sdram_wr_ack_d2 <=  sdram_wr_ack_d1;	
	end
end

//读响应信号打拍同步，降低亚稳态
always@(posedge sdram_clk or negedge sdram_rst_n)begin
    if(!sdram_rst_n)begin
        sdram_rd_ack_d1 <=  1'b0;
        sdram_rd_ack_d2 <=  1'b0;	
	end
    else begin
        sdram_rd_ack_d1 <=  sdram_rd_ack;
		sdram_rd_ack_d2 <=  sdram_rd_ack_d1;	
	end
end

//检测读写响应信号下降沿,下降沿位置表明读写响应结束
assign  sdram_wr_ack_fall = (sdram_wr_ack_d2 & ~sdram_wr_ack_d1);
assign  sdram_rd_ack_fall = (sdram_rd_ack_d2 & ~sdram_rd_ack_d1);

//sdram_wr_addr:sdram写地址
always@(posedge sdram_clk or negedge sdram_rst_n)begin
    if(!sdram_rst_n)
        sdram_wr_addr <= 24'd0;
    else if(wr_fifo_rst)
        sdram_wr_addr <= sdram_wr_b_addr;							//复位fifo则地址为sdram首地址
    else if(sdram_wr_ack_fall) 										//一次突发写结束,更改sdram写地址
        begin                                          //判断突发写结束的时候是否到达了写的末地址
            if(sdram_wr_addr < (sdram_wr_e_addr - wr_burst_len))                       
                sdram_wr_addr   <=  sdram_wr_addr + wr_burst_len;	//未达到末地址,写地址累加
            else        
                sdram_wr_addr   <=  sdram_wr_b_addr;				//到达末地址,回到写的首地址
        end
end		

//sdram_rd_addr:sdram读地址
always@(posedge sdram_clk or negedge sdram_rst_n)begin
    if(!sdram_rst_n)
        sdram_rd_addr <= 24'd0;
    else if(rd_fifo_rst)
        sdram_rd_addr   <=  sdram_rd_b_addr;
    else if(sdram_rd_ack_fall) 											//一次突发读结束,更改读地址
        begin                                          //判断突发读结束的时候是否到达了读的末地址
            if(sdram_rd_addr < (sdram_rd_e_addr - rd_burst_len))                    
                sdram_rd_addr   <=  sdram_rd_addr + rd_burst_len;	//读地址未达到末地址,读地址累加
            else    
                sdram_rd_addr   <=  sdram_rd_b_addr;				//到达末地址,回到读地址的首地址
        end
end

//sdram_wr_req,sdram_rd_req:读写请求信号
always@(posedge sdram_clk or negedge sdram_rst_n)begin
    if(!sdram_rst_n)
        begin
            sdram_wr_req <= 1'b0;
            sdram_rd_req <= 1'b0;
        end
    else if(init_end)   	//初始化完成后响应读写请求						
        begin   									//优先执行写操作，防止写入SDRAM中的数据丢失
            if(wr_fifo_num >= wr_burst_len)begin   	//写FIFO中的数据量达到写突发长度，写请求拉高
                sdram_rd_req <=  1'b0;
                sdram_wr_req <=  1'b1;   			
			end                                   //读FIFO中的数据量小于读突发长度,且读使能信号有效，则读请求拉高
            else if((rd_fifo_num < rd_burst_len) && (read_valid))begin 
                sdram_wr_req <=  1'b0;
				    sdram_rd_req <=  1'b1;   			
            end
            else begin
				    sdram_wr_req <=  1'b0;
				    sdram_rd_req <= 1'b0;
			end
        end
    else begin
		sdram_wr_req <= 1'b0;
		sdram_rd_req <= 1'b0;
	end
end

//实例化读写FIFO

//写FIFO
fifo_data   wr_fifo_data(
    //用户接口
    .wrclk      (wr_fifo_wr_clk ),  //写时钟
    .wrreq      (wr_fifo_wr_req ),  //写请求
    .data       (wr_fifo_wr_data),  //写数据
    //SDRAM接口
    .rdclk      (sdram_clk		),  //读时钟
    .rdreq      (sdram_wr_ack   ),  //读请求
    .q          (sdram_data_in  ),  //读数据

    .rdusedw    (wr_fifo_num    ),  //FIFO中的数据量
    .wrusedw    (               ),
    .aclr       (~sdram_rst_n || wr_fifo_rst)  //清零信号
);


//读FIFO
fifo_data   rd_fifo_data(
    //sdram接口
    .wrclk      (sdram_clk		),  //写时钟
    .wrreq      (sdram_rd_ack   ),  //写请求
    .data       (sdram_data_out ),  //写数据
    //用户接口
    .rdclk      (rd_fifo_rd_clk ),  //读时钟
    .rdreq      (rd_fifo_rd_req ),  //读请求
    .q          (rd_fifo_rd_data),  //读数据

    .rdusedw    (               ),
    .wrusedw    (rd_fifo_num    ),  //FIFO中的数据量
    .aclr       (~sdram_rst_n || rd_fifo_rst)  //清零信号
);

endmodule

