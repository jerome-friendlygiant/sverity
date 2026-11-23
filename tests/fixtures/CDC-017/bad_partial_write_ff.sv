// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-009 WARNING
// expect-none: CDC-017
// False Gray: whole Gray write plus a partial overwrite of bit 0 with unrelated data.
module top(input logic clka, clkb, rst_n, inc, sel, d, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 1;
  always_ff @(posedge clka or negedge rst_n)
    if (!rst_n) gray <= '0;
    else begin
      gray <= bin ^ (bin >> 1);
      if (sel) gray[0] <= bin[3];
    end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
