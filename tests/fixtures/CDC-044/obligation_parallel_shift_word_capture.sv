// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign word = v; => assign word = s4;
// revert-expect-none: CDC-044 WARNING
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, output logic [3:0] word);
  logic s1, s2, s3, s4;
  logic [3:0] v;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s4, s3, s2, s1} <= '0;
    else {s4, s3, s2, s1} <= {s3, s2, s1, rx};
  always_ff @(posedge clka) v <= {s4, s3, s2, s1};
  assign word = v;
endmodule
