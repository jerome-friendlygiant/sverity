// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-007 WARNING
// expect-none: CDC-044
module top(input logic clka, rst_n, rx, output logic observed);
  logic s1, s2, s3, s4;
  logic tap_logic;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {s4, s3, s2, s1} <= '0; else {s4, s3, s2, s1} <= {s3, s2, s1, rx};
  assign tap_logic = s1 ^ rx;
  assign observed = s4;
endmodule
