// Authored for the SVerity accuracy set.
// The producer accepts a word only when its local busy flag is clear.
module top(input logic sclk,dclk,input logic [8:0] src_i,output logic [7:0] out_o);
 logic [7:0] held_q=0;
 logic req_q=0,ack_q=0,a1=0,a2=0,r1=0,r2=0;
 always_ff @(posedge sclk) begin
  a1<=ack_q; a2<=a1;
  if(req_q && a2) req_q<=0;
  if(!req_q && !a2 && src_i[8]) begin held_q<=src_i[7:0];req_q<=1;end
 end
 always_ff @(posedge dclk) begin
  r1<=req_q;r2<=r1;
  if(r2 && !ack_q) begin out_o<=held_q;ack_q<=1;end
  if(!r2) ack_q<=0;
 end
endmodule
