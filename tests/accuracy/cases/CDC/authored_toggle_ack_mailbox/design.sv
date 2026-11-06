// Authored for the SVerity accuracy set.
module top(input logic sclk,dclk,input logic [8:0] src_i,output logic [7:0] out_o);
 logic [7:0] held_q=0;
 logic request_q=0,ack_q=0,a1=0,a2=0,r1=0,r2=0;
 always_ff @(posedge sclk) begin
  a1<=ack_q;a2<=a1;
  if(src_i[8] && request_q==a2) begin
    held_q<=src_i[7:0];request_q<=~request_q;
  end
 end
 always_ff @(posedge dclk) begin
  r1<=request_q;r2<=r1;
  if(r2!=ack_q) begin out_o<=held_q;ack_q<=r2;end
 end
endmodule
