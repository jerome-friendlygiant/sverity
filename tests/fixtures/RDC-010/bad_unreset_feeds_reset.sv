// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clka) u <= d; => always_ff @(posedge clka or negedge rst_n) if (!rst_n) u <= 1'b0; else u <= d;
// revert-expect-none: RDC-010 WARNING
// expect: RDC-010 WARNING
module top(input logic clka, rst_n, d, output logic q);
  logic u;
  always_ff @(posedge clka or negedge rst_n) begin
    if (!rst_n) q <= 1'b0; else q <= u;
  end
  always_ff @(posedge clka) u <= d;
endmodule
