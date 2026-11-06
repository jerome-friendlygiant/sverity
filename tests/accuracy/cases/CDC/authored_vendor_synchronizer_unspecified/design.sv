// Authored for the SVerity accuracy set.
(* blackbox *) module vendor_sync(input logic clk,d,output wire q); endmodule
module top(input logic sclk,dclk,input logic src_i,output logic out_o);
 logic status_q=0;
 wire status_sync;
 always_ff @(posedge sclk) status_q<=status_q|src_i;
 vendor_sync cell_u(.clk(dclk),.d(status_q),.q(status_sync));
 always_ff @(posedge dclk) out_o<=status_sync;
endmodule
