// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic src_i, output logic out_o);
 logic toggle_q=0,meta_q=0,sync_q=0,old_q=0;
 always_ff @(posedge sclk) if(src_i) toggle_q<=~toggle_q;
 always_ff @(posedge dclk) begin
   meta_q<=toggle_q;
   sync_q<=meta_q;
   old_q<=sync_q;
   out_o<=meta_q ^ sync_q;
 end
endmodule
