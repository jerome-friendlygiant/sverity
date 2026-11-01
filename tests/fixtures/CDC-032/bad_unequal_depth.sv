// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: logic b0s1, b0s2, b1s1, b1s2, b1s3; => logic b0s1, b0s2, b1s1, b1s2;
// revert: {b0s2, b0s1, b1s3, b1s2, b1s1} <= '0; else {b0s2, b0s1, b1s3, b1s2, b1s1} <= {b0s1, a[0], b1s2, b1s1, a[1]}; => {b0s2, b0s1, b1s2, b1s1} <= '0; else {b0s2, b0s1, b1s2, b1s1} <= {b0s1, a[0], b1s1, a[1]};
// revert: assign q = {b1s3, b0s2}; => assign q = {b1s2, b0s2};
// revert-expect-none: CDC-032 ERROR
// Bit 0 passes through two stages, bit 1 through three: the bus is incoherent.
// expect: CDC-032 ERROR
module top(input logic clka, clkb, rst_n, input logic [1:0] d, output logic [1:0] q);
  logic [1:0] a;
  logic b0s1, b0s2, b1s1, b1s2, b1s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a <= '0; else a <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {b0s2, b0s1, b1s3, b1s2, b1s1} <= '0; else {b0s2, b0s1, b1s3, b1s2, b1s1} <= {b0s1, a[0], b1s2, b1s1, a[1]};
  assign q = {b1s3, b0s2};
endmodule
