module test01(
		input clk_in,
		input rst_n,
		output clk_out);
                

		localparam div_width = 3, //分频计数器位宽
		            fre_div = 7; //分频数
		
		reg clk_even;
		reg [div_width-1:0] clk_even_cnt;  //偶数计数器
		reg clk_1,clk_2;
		wire clk_odd;
		reg [div_width-1:0] clk_1_cnt,clk_2_cnt; //奇数计数器

                     assign clk_out = (fre_div == 1'b1)? clk_in : (fre_div[0]? clk_odd : clk_even); //输出
                     assign clk_odd = clk_1 | clk_2;

//偶数分频情况
	 	always @ (posedge clk_in or negedge rst_n)
			begin
			       if (!rst_n)
			            begin
				            clk_even <= 1'b0;
    				        clk_even_cnt <= 0;
			            end
		           else if (clk_even_cnt == fre_div/2-1)
                        begin
				            clk_even <= ~clk_even;
    				        clk_even_cnt <= clk_even_cnt + 1'b1;
			            end
                   else if (clk_even_cnt == fre_div - 1)
                        begin
				            clk_even <= ~clk_even;
    			            clk_even_cnt <= 0;
			            end
		           else
                        begin
			             	clk_even <= clk_even;
    				        clk_even_cnt <= clk_even_cnt + 1'b1;
			            end
			end


//奇数分频情况
       always @ (posedge clk_in or negedge rst_n)
			begin
			      if (!rst_n)
			            begin
				           clk_1 <= 1'b0;
    				       clk_1_cnt <= 0;
			            end
		          else if (clk_1_cnt  == (fre_div-1)/2)
                        begin
				           clk_1 <= ~clk_1;
    				       clk_1_cnt  <= clk_1_cnt + 1'b1;
			            end
                 else if (clk_1_cnt  == fre_div - 1)
                        begin
				           clk_1 <= ~clk_1;
    				       clk_1_cnt  <= 0;
			            end
		        else
                        begin
				           clk_1 <= clk_1;
    				       clk_1_cnt  <= clk_1_cnt  + 1'b1;
			            end
			end

      always @ (negedge clk_in or negedge rst_n)
			begin
			       if (!rst_n)
			           begin
				         clk_2 <= 1'b0;
    				     clk_2_cnt <= 0;
			           end
		          else if (clk_2_cnt  == (fre_div-1)/2)
                       begin
				         clk_2 <= ~clk_2;
    				     clk_2_cnt  <= clk_2_cnt  + 1'b1;
			           end
                 else if (clk_2_cnt  == fre_div - 1)
                        begin
  				           clk_2 <= ~clk_1;
  				           clk_2_cnt  <= 0;
			           end
		        else
                       begin
				         clk_2 <= clk_1;
    				     clk_2_cnt  <= clk_2_cnt  + 1'b1;
			            end
			end

endmodule

