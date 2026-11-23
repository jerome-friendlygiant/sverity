// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: s2[0] <= gray[0]; => s1[0] <= gray[0]; s2[0] <= s1[0];
// revert-expect-none: CDC-017 ERROR
// expect: CDC-017 ERROR
// expect-confidence: CDC-017 established
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 4'd1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) begin
    s1 <= '0;
    s2 <= '0;
  end else begin
    s1[3:1] <= gray[3:1];
    s2[3:1] <= s1[3:1];
    s2[0] <= gray[0];
  end
  assign q = s2;
endmodule
