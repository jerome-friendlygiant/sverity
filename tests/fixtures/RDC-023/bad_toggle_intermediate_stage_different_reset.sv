// sverity-args: --goal rdc --option CDC-005.min_stages=5 --top top --sdc @FIXTURES@/_common/async.sdc
// Required intermediate synchronizer stage s4 between early detector (s2 ^ s3) and output stage s5 uses a different reset.
// expect: RDC-023 WARNING
// expect-unresolved: RDC-023
// expect-none: RDC-023 INFO
module top(input logic clka, clkb, rst_n, rst2_n, send, output logic event_b, output logic out_b);
  logic toggle_a, s1, s2, s3, s4, s5;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) toggle_a <= 1'b0; else if (send) toggle_a <= ~toggle_a;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {s3, s2, s1} <= 3'b000; else {s3, s2, s1} <= {s2, s1, toggle_a};
  always_ff @(posedge clkb or negedge rst2_n)
    if (!rst2_n) s4 <= 1'b0; else s4 <= s3;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) s5 <= 1'b0; else s5 <= s4;
  assign event_b = s2 ^ s3;
  assign out_b = s5;
endmodule
