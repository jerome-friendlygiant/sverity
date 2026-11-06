// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk, input logic src_i, output logic out_o);
 logic [3:0] write_q=0,read_q=0;
 logic [3:0] write_meta=0,write_sync=0;
 always_ff @(posedge sclk) if(src_i) write_q<=write_q+1'b1;
 always_ff @(posedge dclk) begin
   write_meta<=write_q; write_sync<=write_meta;
   if(!out_o) read_q<=read_q+1'b1;
   out_o<=(read_q==write_sync);
 end
endmodule
