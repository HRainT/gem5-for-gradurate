//固定优先级、轮询：https://blog.csdn.net/spx1164376416/article/details/124377534
module fixed_pri_arb#(
			parameter REQ_WIDTH = 16)
		(
			input [ REQ_WIDTH-1:0] req;
			output reg [ REQ_WIDTH-1:0] grant
			);
			

	assign gnt = req& (~(req-1)); //这里的req-1中的1实际上就相当于优先级一直是000....001，而且req-pri一定不会是负数，所以也不需要拓展两倍的位宽
		
	
endmodule


`timescale 1ns / 1ns
module round_robin #(
    parameter N = 8
    )
    (
        input           rst_n,
        input           clk,
        input [N-1:0]   req,
        output [N-1:0]  grant
    );
reg [N-1:0] one_hot_priority;  //   比如0010   代表的优先级为 1230   用独热码表示其优先级
reg [2*N-1:0] grant_double;

reg [N-1:0] one_hot_priority_q ;
always @(posedge clk or negedge rst_n)begin
    if(!rst_n) begin
        one_hot_priority_q <= 8'b1;
    end
    else begin
        one_hot_priority_q <= one_hot_priority;
    end
end

//在每次仲裁之后,都要算出此时变化后的优先级,所以要用组合逻辑
always @(*)begin
    if(!rst_n) begin
        one_hot_priority = 8'b1;
    end
    else if (grant != 'd0)begin
        one_hot_priority = {grant[N-2:0],grant[N-1]};  //循环左移
    end
    else begin
        one_hot_priority = one_hot_priority_q;
    end
end

//在下一个时钟周期输出,grant_double
always @(posedge clk or negedge rst_n)begin
    if(!rst_n) begin
        grant_double  <= 16'b0;
    end
    else begin
        grant_double  <= {req,req} & ~({req,req}-{{N{1'b0}},one_hot_priority});
    end
end
assign grant = grant_double[2*N-1:N] | grant_double[N-1:0];
endmodule