module top_cdc_rfc1_t10(input logic src_clk, dst_clk, rst_n,
 input logic src_req,
 input logic [7:0] src_payload,
 output logic dst_done,
 output logic [7:0] dst_payload,
 output logic src_done_seen
);
     logic q1,q2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin q1<=0;q2<=0;dst_payload<=0;dst_done<=0;end else begin q1<=src_req;q2<=q1;dst_done<=q2;if(q2) dst_payload<=src_payload;end
 always_ff @(posedge src_clk or negedge rst_n) if(!rst_n) src_done_seen<=0; else src_done_seen<=dst_done;
endmodule
