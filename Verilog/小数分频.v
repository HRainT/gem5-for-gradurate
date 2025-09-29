//小数分频电路设计
//双模前置法实现5.4分频
//https://blog.csdn.net/Loudrs/article/details/130108096
module clk_div_fraction
   (
    input				rst_n,		//复位信号
    input               clk,		//时钟信号
    output              clk_frac	//小数分频输出信号
    );
 
 //定义介于5.4分频的5分频和6分频
parameter		CLK_DIV_1  =  5;
parameter    	CLK_DIV_2  =  6;
parameter    	DIFF       =  4;		//10个周期内5分频与5.4分频的差值


reg [3:0]            cnt_end;			//分频插入计数器	（用于判断插入什么分频）	
reg [3:0]            cnt;				//总计数器
reg                  clk_frac_r; 		//小数分频中间寄存器信号
reg [4:0]            diff_cnt_r;		//差值信号
reg [4:0]            diff_cnt;			//差值信号
wire                 diff_cnt_en= cnt == cnt_end;		//使能信号

//差值累加逻辑模块
always @(*) begin
    if(diff_cnt_r >= 10) begin
        diff_cnt = diff_cnt_r -10 + DIFF;	//差值大于10，插入6分频，差值减6
    end
    else begin
        diff_cnt = diff_cnt_r + DIFF;		//差值小于10，插入5分频，差值加4
    end
end
  
// 借用寄存器延迟输出diff_cnt_r                           
always @(posedge clk or negedge rst_n) begin
    if(!rst_n) begin				//复位差值清零
        diff_cnt_r <= 0;
    end
    else if(diff_cnt_en) begin		//使能信号高电平时，差值信号延迟输出
        diff_cnt_r <= diff_cnt;
    end
end

//5分频和6分频插入逻辑模块
always @(posedge clk or negedge rst_n) begin
    if(!rst_n) begin
        cnt_end <= CLK_DIV_1-1 ;		//复位先插入5分频
    end
    else if(diff_cnt >= 10) begin
        cnt_end <= CLK_DIV_2-1 ;		//差值大于10，插入6分频
    end
    else begin
        cnt_end <= CLK_DIV_1-1 ;		//差值小于10，插入5分频
    end
end

//
always @(posedge clk or negedge rst_n) begin
    if(!rst_n) begin					//总计数器、分频信号信号复位
        cnt <= 1'b0;
        clk_frac_r <= 1'b0;
    end
    else if(cnt == cnt_end) begin		//计数器到分频插入界限点
        cnt <= 1'b0;					//总计数器清零
        clk_frac_r <= 1'b1;				//时钟分频信号电平置"1"
    end
    else begin			//其他情况下，计数器累加计数、时钟分频信号电平保持"0"
        cnt <= cnt + 1'b1;		
        clk_frac_r <= 1'b0;
    end
end

//延时输出，消除亚稳态  
assign clk_frac = clk_frac_r;

endmodule
