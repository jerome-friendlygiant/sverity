module top_cdc_rfc1_t29(input logic src_clk, dst_clk, rst_n, load_cmd,
 input logic [7:0] src_data,
 output logic [7:0] dst_data
);
    logic load,c1,c2;
 always_ff @(posedge src_clk or negedge rst_n) if(!rst_n) load<=0; else load<=load_cmd;
 always_ff @(posedge dst_clk or negedge rst_n) if(!rst_n) begin c1<=0;c2<=0;dst_data<=0;end else begin c1<=load;c2<=c1;if(c2) dst_data<=src_data;end
endmodule
