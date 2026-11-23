// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-017 ERROR
// expect-confidence: CDC-017 heuristic
// Binary counter disguised by a whole Gray-form write and a range-select counter write.
module top(input logic clka, clkb, rst_n, load, output logic [3:0] q);
  logic [3:0] cnt, s1, s2;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) cnt <= '0;
    else begin
      cnt <= cnt ^ (cnt >> 1);
      if (load) cnt[3:0] <= cnt + 1'b1;
    end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, cnt};
  assign q = s2;
endmodule
