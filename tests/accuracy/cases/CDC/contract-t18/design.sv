module top_cdc_rfc1_t18(input logic src_clk, dst_clk, rst_n, send,
 input logic [7:0] payload_in,
 output logic [7:0] dst_payload,
 output logic src_ack
);
 logic src_req; logic [7:0] src_payload; logic r1,r2,ack_dst,a1,a2;
 always_ff @(posedge src_clk or negedge rst_n)
   if (!rst_n) begin src_req<=0; src_payload<=0; a1<=0; a2<=0; end
   else begin a1<=ack_dst; a2<=a1;
     if (send && !src_req && !a2) begin src_req<=1; src_payload<=payload_in; end
     else if (a2) src_req<=0;
   end
 always_ff @(posedge dst_clk or negedge rst_n)
   if (!rst_n) begin r1<=0; r2<=0; dst_payload<=0; ack_dst<=0; end
   else begin r1<=src_req; r2<=r1;
     if (r2 && !ack_dst) begin dst_payload<=src_payload; ack_dst<=1; end
     if (!r2) ack_dst<=0;
   end
 assign src_ack=a2;
endmodule
