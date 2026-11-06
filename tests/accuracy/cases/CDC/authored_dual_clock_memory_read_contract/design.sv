// Authored for the SVerity accuracy set.
(* blackbox *) module memory_macro(input logic wclk,rclk,wen,input logic [7:0] data,output wire [7:0] q); endmodule
module top(input logic sclk,dclk,input logic [8:0] src_i,output wire [7:0] out_o);
 memory_macro ram_u(.wclk(sclk),.rclk(dclk),.wen(src_i[8]),.data(src_i[7:0]),.q(out_o));
endmodule
