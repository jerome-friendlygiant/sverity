// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Mixed direct and one-hop captured bits with distinct effective cycle depths form an observable shift word.
// expect: CDC-044 WARNING
// expect-confidence: CDC-044 heuristic
module top(input logic clka, rst_n, rx, output logic [1:0] word);
  logic s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s3, s2, s1} <= '0;
    else {s3, s2, s1} <= {s2, s1, rx};
  always_ff @(posedge clka) word[0] <= s3;
  assign word[1] = s2;
endmodule
