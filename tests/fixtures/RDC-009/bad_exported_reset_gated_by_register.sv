// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// An output reset built from rst_n and an enable register, used by no register of this design: the consumer is outside the design.
// expect: RDC-009 WARNING
// expect-unresolved: RDC-009
module top(input logic clka, rst_n, d, output logic q, rst_o);
  logic en;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin en <= 1'b0; q <= 1'b0; end else begin en <= d; q <= d; end
  assign rst_o = rst_n & en;
endmodule
