module top(input logic clk_a, clk_b, rst_n, input logic step, output logic [7:0] out);
logic [3:0] bin; logic [3:0] gray; logic [3:0] g1,g2;
always_ff @(posedge clk_a or negedge rst_n) if(!rst_n) bin<=0; else if(step) bin<=bin+1'b1;
assign gray=bin^(bin>>1);
always_ff @(posedge clk_b or negedge rst_n) if(!rst_n) begin g1<=0;g2<=0; end else begin g1<=gray;g2<=g1; end
assign out={4'b0,g2}; endmodule
