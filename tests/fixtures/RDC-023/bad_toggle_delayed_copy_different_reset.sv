// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Source and synchronizer stages share rst_n, while the detector's delayed copy resets on rst2_n.
// expect: RDC-023 WARNING
// expect-unresolved: RDC-023
// expect-none: RDC-023 INFO
module top(input logic clka, clkb, rst_n, rst2_n, send, output logic event_b);
  logic toggle_a, s1, s2, delayed;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) toggle_a <= 1'b0; else if (send) toggle_a <= ~toggle_a;
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) {s2, s1} <= 2'b00; else {s2, s1} <= {s1, toggle_a};
  always_ff @(posedge clkb or negedge rst2_n)
    if (!rst2_n) delayed <= 1'b0; else delayed <= s2;
  assign event_b = s2 ^ delayed;
endmodule
