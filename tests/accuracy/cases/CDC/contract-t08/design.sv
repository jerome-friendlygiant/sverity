module top_cdc_rfc1_t08(input logic src_clk, dst_clk, rst_n,
 input logic src_event,
 output logic event_pulse
);
   logic t1,t2;
 logic src_toggle;
 always_ff @(posedge src_clk or negedge rst_n) if (!rst_n) src_toggle<=0; else if (src_event) src_toggle<=~src_toggle;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin t1<=0;t2<=0;event_pulse<=0;end else begin t1<=src_toggle;t2<=t1;event_pulse<=t2;end
endmodule
