// A release order between two primary inputs on a declared clock: an environment assumption (INFO), not checked.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/port_order.json
// expect: CST-016 INFO
// expect-count: CST-016 INFO 1
// expect-none: CST-016 WARNING
module top(input logic clka, clkb, rst_a_n, rst_b_n, d, output logic q, r);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) r <= 1'b0; else r <= d;
endmodule
