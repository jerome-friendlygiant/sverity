// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// Full 4-bit synchronized Gray pointer plus an independent 1-bit debug capture is CDC-003 on dbg, not CDC-017.
// expect: CDC-003 ERROR
// expect-none: CDC-017 ERROR
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q, output logic q2);
  logic [3:0] bin, gray, s1, s2;
  logic dbg;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 4'd1;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) gray <= '0; else gray <= bin ^ (bin >> 1);
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1, dbg} <= '0; else begin
    s1 <= gray;
    s2 <= s1;
    dbg <= gray[0];
  end
  assign q = s2;
  assign q2 = dbg;
endmodule
