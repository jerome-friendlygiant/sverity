module case_ci01(input logic clk, input logic rst_n, input logic async_req, output logic req_seen);
 logic s1,s2; always_ff @(posedge clk or negedge rst_n) if(!rst_n) begin s1<=0; s2<=0; end else begin s1<=async_req; s2<=s1; end
 always_ff @(posedge clk or negedge rst_n) if(!rst_n) req_seen<=0; else req_seen<=s2;
endmodule
