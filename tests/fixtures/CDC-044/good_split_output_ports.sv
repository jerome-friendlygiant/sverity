// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-none: CDC-044
module top(input logic clka, rst_n, rx, output logic newest, older);
  logic s1, s2, s3, s4;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) {s4, s3, s2, s1} <= '0; else {s4, s3, s2, s1} <= {s3, s2, s1, rx};
  assign newest = s4;
  assign older = s3;
endmodule
