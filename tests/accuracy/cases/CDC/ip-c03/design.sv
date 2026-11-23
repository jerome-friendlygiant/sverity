// C03
module c03(input logic clk_d, input logic async_req, output logic req_q);
  (* ASYNC_REG = "TRUE" *) logic sync1;
  (* ASYNC_REG = "TRUE" *) logic sync2;
  always_ff @(posedge clk_d) begin sync1 <= async_req; sync2 <= sync1; end
  assign req_q = sync2;
endmodule
