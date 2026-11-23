module case_ib04(input logic clk, input logic rst_n, input logic din, output logic dout);
 logic x; opaque_sync u_sync(.clk(clk),.rst_n(rst_n),.d(din),.q(x)); always_ff @(posedge clk or negedge rst_n) if(!rst_n) dout<=0; else dout<=x;
endmodule

(* blackbox *) module opaque_sync(input logic clk, input logic rst_n, input logic d, output logic q); endmodule
