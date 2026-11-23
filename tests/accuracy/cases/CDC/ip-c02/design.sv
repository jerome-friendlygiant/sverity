// C02
module c02(input logic clk_d, input logic async_req, output logic req_q);
  always_ff @(posedge clk_d) req_q <= async_req;
endmodule
