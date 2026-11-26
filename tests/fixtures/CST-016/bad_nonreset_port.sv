// `d` is a primary input used only as data: the declaration is unapplied, and the real reset's clause stays Assumed.
// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/nonreset_port.json
// expect: CST-016 WARNING
// expect-count: CST-016 WARNING 1
// expect-count: CST-016 INFO 1
// expect-coverage: reset-contracts INCOMPLETE
module top(input logic clka, clkb, rst_a_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0; else q <= d;
endmodule
