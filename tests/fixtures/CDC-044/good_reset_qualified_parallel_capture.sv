// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Reset-qualified one-hop captures are not unconditional exact copies for parallel shift-word detection.
// expect-none: CDC-044
module top(input logic clka, rst_n, srst_n, rx, output logic [1:0] word_a, output logic [1:0] word_b);
  logic s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) {s3, s2, s1} <= '0;
    else {s3, s2, s1} <= {s2, s1, rx};
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) word_a <= 2'b00;
    else word_a <= {s3, s2};
  always_ff @(posedge clka)
    if (!srst_n) word_b <= 2'b00;
    else word_b <= {s3, s2};
endmodule
