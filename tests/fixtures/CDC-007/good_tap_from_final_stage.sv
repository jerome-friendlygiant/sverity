// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-007
// Near miss of bad_middle_stage_tap.sv: the tap is taken from the final stage s3 instead of the middle stage s2, so no non-final stage feeds anything but the next stage.
module top(input logic clka, clkb, rst_n, d, output logic q, tap);
  logic a_q, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s3, s2, s1} <= '0; else {s3, s2, s1} <= {s2, s1, a_q};
  assign q = s3;
  assign tap = s3;
endmodule
