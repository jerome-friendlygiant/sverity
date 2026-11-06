// Authored for the SVerity accuracy set.
module sticky_capture(input logic clk,async_i,output logic sync_o);
 logic meta_q=0;
 always @(posedge clk) begin meta_q<=async_i;sync_o<=meta_q;end
 initial sync_o=0;
endmodule
module top(input logic sclk,dclk,input logic src_i,output logic out_o);
 logic status_q=0;
 always @(posedge sclk) status_q<=status_q|src_i;
 sticky_capture capture(.clk(dclk),.async_i(status_q),.sync_o(out_o));
endmodule
