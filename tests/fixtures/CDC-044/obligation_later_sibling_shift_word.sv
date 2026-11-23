// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: assign word = {s4, s3}; => assign word = s4;
// revert-expect-none: CDC-044 WARNING
// expect: CDC-044 WARNING
module top(input logic clka, rst_n, rx, output logic level, output logic [1:0] word);
  logic a1, a2, s1, s2, s3, s4;
  always_ff @(posedge clka) begin a1 <= rx; a2 <= a1; end
  always_ff @(posedge clka) begin s1 <= rx; s2 <= s1; s3 <= s2; s4 <= s3; end
  assign level = a2;
  assign word = {s4, s3};
endmodule
