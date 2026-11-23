// sverity-args: --goal rdc --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// The receiver and source share a reset. The detector's delayed copy resets to a different value.
// expect: RDC-023 WARNING
// expect-unresolved: RDC-023
// expect-none: RDC-023 INFO
module top(input logic clka, clkb, rst_n, send, output logic event_b);
  logic toggle_a, s1, s2, s3, delayed;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) toggle_a <= 0; else if (send) toggle_a <= ~toggle_a;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {delayed, s3, s2, s1} <= 4'b1000;
    else {delayed, s3, s2, s1} <= {s3, s2, s1, toggle_a};
  assign event_b = s3 ^ delayed;
endmodule
