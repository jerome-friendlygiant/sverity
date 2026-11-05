// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Two separately synchronised signals meet at y. The unused s3 and t3 do not change that.
// expect: CDC-010 WARNING
module top(input logic clka, clkb, d, e, output logic y);
  logic src, src2, s1, s2, s3, t1, t2, t3;
  always_ff @(posedge clka) begin src <= d; src2 <= e; end
  always_ff @(posedge clkb) begin
    s1 <= src;  s2 <= s1; s3 <= s2;
    t1 <= src2; t2 <= t1; t3 <= t2;
    y <= s2 & t2;
  end
endmodule
