//https://www.nowcoder.com/practice/2bf1b28a4e634d1ba447d3495134baac?tpId=311&tqId=5000697&sourceUrl=%2Fexam%2Foj%3Fpage%3D1%26tab%3DVerilog%25E7%25AF%2587%26topicId%3D311
//题目要求：分别编写一个数据发送模块和一个数据接收模块，模块的时钟信号分别为clk_a，clk_b。两个时钟的频率不相同。数据发送模块循环发送0-7，在每个数据传输完成之后，间隔5个时钟，发送下一个数据。请在两个模块之间添加必要的握手信号，保证数据传输不丢失。
//data_req和data_ack的作用说明：
//data_req表示数据请求接受信号。当data_out发出时，该信号拉高，在确认数据被成功接收之前，保持为高，期间data应该保持不变，等待接收端接收数据。
//当数据接收端检测到data_req为高，表示该时刻的信号data有效，保存数据，并拉高data_ack。
//当数据发送端检测到data_ack，表示上一个发送的数据已经被接收。撤销data_req，然后可以改变数据data。等到下次发送时，再一次拉高data_req。

//核心：在快时钟域的一侧，采用边沿检测+取反操作。这样做有两方面考虑：
//1.通过上升沿采样方法采集慢时钟域数据，是边沿检测同步器。采样后生成脉冲有效信号req_pos。
//2.根据req_pos生成持续电平的ack信号，用翻转来表示信号有效。这种持续的有效电平相当于帮慢时钟域采样做完了脉冲同步器的前半部分，也就是信号展宽的工作，这样慢时钟域采样后只需要做一个双边沿检测器就能采样到快时钟域信号翻转的脉冲信号
`timescale 1ns/1ns

module data_driver(
    input clk_a,
    input rst_n,
    input data_ack,
    output reg [3:0]data,
    output reg data_req
    );
reg [2:0] cnt;
reg [2:0] ack_r;
wire ack_pos;

//assign ack_pos = ack_r[1]&(~ack_r[2]);
assign ack_pos = ack_r[1]^ack_r[2];

always @(posedge clk_a&nbs***bsp;negedge rst_n) begin
    if(~rst_n) begin
        data  <= 4'b0;      
    end 
    else if(ack_pos) begin
        if(data == 4'd7)
            data  <= 4'b0;
        else 
            data  <= data + 1'b1;
    end        
end    

always @(posedge clk_a&nbs***bsp;negedge rst_n) begin
    if(~rst_n) begin
        data_req  <= 1'b0;      
    end 
    else if(ack_pos)
        data_req <= 1'b0;
    else if(cnt == 3'd4)
        data_req <= 1'd1;
end 
always @(posedge clk_a&nbs***bsp;negedge rst_n) begin
    if(~rst_n) 
        cnt <= 3'd0;
    else if(ack_pos)
        cnt <= 3'd0;
    else if(~data_req)
        cnt <= cnt + 1'b1;
end 

always @(posedge clk_a&nbs***bsp;negedge rst_n) begin
    if(~rst_n) 
        ack_r <= 3'b000;
    else
        ack_r <= {ack_r[1:0],data_ack};
end 

endmodule

module data_receiver(
    input clk_b,
    input rst_n,
    output reg data_ack,
    input [3:0]data,
    input data_req
    );
 reg [3:0] data_reg;
 reg [2:0] req_r;
 wire req_pos;

 assign req_pos = req_r[1]&(~req_r[2]);

 always @(posedge clk_b&nbs***bsp;negedge rst_n) begin
     if(~rst_n) begin
         req_r <= 3'b0;      
     end 
     else begin
         req_r <= {req_r[1:0],data_req};
     end        
 end   

 /*always @(posedge clk_b&nbs***bsp;negedge rst_n) begin
     if(~rst_n) begin
         data_ack <= 1'b0;      
     end 
     else if(req_pos)
        data_ack <= 1'b1;
    else
        data_ack <= 1'b0;

 end */
 always @(posedge clk_b&nbs***bsp;negedge rst_n) begin
     if(~rst_n) begin
         data_ack <= 1'b0;      
     end 
     else if(req_pos)
        data_ack <= ~data_ack;
end 

always @(posedge clk_b&nbs***bsp;negedge rst_n) begin
     if(~rst_n)
        data_reg <= 4'd0;
    else if(req_pos)
        data_reg <= data;
end 

endmodule           