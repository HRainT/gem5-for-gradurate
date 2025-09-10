module traffic (
    input clk,
    input rst_n,
    output reg[2:0] light[1:0]
);
    reg[2:0] state;
    reg[7:0] count;
    reg[24:0] timer;
    parameter S1 = 3'b001; 
    parameter S2 = 3'b010; 
    parameter S3 = 3'b011; 
    parameter S4 = 3'b100;
    always @(posedge clk or negedge rst_n) begin
        if(!rst_n) begin
            count <= 8'b0;
            timer <= 16'b0;
        end 
        else if(timer == 16'd1000000)begin
            if(count == 8'd60)begin
                count <= 8'b0;
                timer <= 16'b0;
            end
            else begin
                count <= count + 1;
                timer <= 16'b0;
            end
        end
        else begin
            timer <= timer + 1;
        end
    end
    always@(posedge clk or negedge rst_n)begin
        if(!rst_n) begin
            state <= S1;
            light[0] <= 3'b001; // 
            light[1] <= 3'b100; // 
        end
        else begin
            case(state)
                S1: begin
                    if(count == 8'd25)begin
                        state <= S2;
                        light[0] <= 3'b010; // yellow
                        light[1] <= 3'b100; // red
                    end
                end
                S2: begin
                    if(count == 8'd30) begin
                        state <= S3;
                        light[0] <= 3'b100; // red
                        light[1] <= 3'b001; // green
                    end
                end
                S3: begin
                    if(count == 8'd55) begin
                        state <= S4;
                        light[0] <= 3'b100; // red
                        light[1] <= 3'b010; // yellow
                    end
                end
                S4: begin
                    if(count == 8'd60) begin
                        state <= S1;
                        light[0] <= 3'b001; 
                        light[1] <= 3'b100; // yellow
                    end
                end
                default: state <= S1;
            endcase
        end
    end
endmodule