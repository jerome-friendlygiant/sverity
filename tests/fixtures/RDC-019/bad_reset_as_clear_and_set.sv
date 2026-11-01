// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: if (!rst_n) r <= 1'b1; => if (!rst_n) r <= 1'b0;
// revert-expect-none: RDC-019 WARNING
// One reset net clears one register and sets another.
// expect: RDC-019 WARNING
module top(input logic clka, rst_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r <= 1'b1; else r <= d;
endmodule
