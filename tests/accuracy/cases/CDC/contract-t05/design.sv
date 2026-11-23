module top_cdc_rfc1_t05(input logic src_clk, dst_clk, rst_n,
 input logic src_valid,
 input logic [7:0] src_payload,
 output logic [7:0] dst_payload,
 output logic dst_valid
);
    logic v1,v2;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin v1<=0;v2<=0;dst_payload<=0;end else begin v1<=src_valid;v2<=v1;dst_payload<=src_payload;end
 assign dst_valid=v2;
endmodule
