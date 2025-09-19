module sync_fifo#(
parameter DATA_WIDTH = 8,
parameter DEPTH = 16,
parameter ADDR_WIDTH = 4
)
(
    input clk,
    input rst_n,
    input wr_en,
    input [DATA_WIDTH-1:0]din,
    input rd_en,
    output reg [DATA_WIDTH-1:0]dout,
    output reg full,
    output reg empty
)
    reg [DATA_WIDTH-1:0] mem [DEPTH-1:0];
    reg [ADDR_WIDTH :0] wr_ptr, wr_ptr_next;
    reg [ADDR_WIDTH :0] rd_ptr, rd_ptr_next;
    reg wr_inc;
    reg rd_inc;
    assign wr_inc = wr_en & ~full;
    assign rd_inc = rd_en & ~empty;
    assign wr_ptr_next = wr_ptr + wr_inc;
    assign rd_ptr_next = rd_ptr + rd_inc;

    always@(posedge clk or negedge rst_n)begin
        if(!rst_n)begin
            wr_ptr <= 0;
            rd_ptr <= 0;
            dout <= 0;
        end
        else begin
            wr_ptr <= wr_ptr_next;
            rd_ptr <= rd_ptr_next;
            if(wr_inc)
                mem[wr_ptr[ADDR_WIDTH-1:0]] <= din;
            if(rd_inc)
                dout <= mem[rd_ptr[ADDR_WIDTH-1:0]];
        end
    end

    assign full = (wr_ptr[ADDR_WIDTH] != rd_ptr[ADDR_WIDTH]) && (wr_ptr[ADDR_WIDTH-1:0] == rd_ptr[ADDR_WIDTH-1:0]);//假满
    assign empty = (wr_ptr == rd_ptr);
endmodule