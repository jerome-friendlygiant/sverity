// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/polarity_low.json
// revert: always_ff @(posedge clka or posedge rst_a_n) if (rst_a_n) q <= 1'b0; => always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) q <= 1'b0;
// revert-expect-none: CST-016 WARNING
// expect: CST-016 WARNING
// The injected defect: the register is reset by an active-high use while the contract declares active low.
module top(input logic clka, clkb, rst_a_n, d, output logic q);
  always_ff @(posedge clka or posedge rst_a_n) if (rst_a_n) q <= 1'b0; else q <= d;
endmodule
