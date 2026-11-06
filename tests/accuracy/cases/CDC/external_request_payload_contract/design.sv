module top(input logic src_clk,
  dst_clk,
  request,
  
  input logic [7:0] payload,
  output logic [7:0] result);

  logic request_meta, request_dst, seen;
  always_ff @(posedge dst_clk) begin
    request_meta <= request;
    request_dst <= request_meta;
    seen <= request_dst;
    if (request_dst && !seen) result <= payload;
  end

endmodule
