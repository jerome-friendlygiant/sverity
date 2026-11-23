module top_cdc_rfc1_t07(input logic src_clk, dst_clk, rst_n,
 input logic src_req,
 input logic [7:0] src_payload,
 output logic [7:0] dst_payload
);
    logic r1,r2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin r1<=0;r2<=0;dst_payload<=0;end else begin r1<=src_req;r2<=r1;if(r2 && !r1) dst_payload<=src_payload;end
endmodule
