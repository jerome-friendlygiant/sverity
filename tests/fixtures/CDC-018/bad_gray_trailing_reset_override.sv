// sverity-args: --top top --sdc @DIR@/skew.sdc
// A second constant write in the Gray reset arm overrides the consistent reset value.
// expect: CDC-018 WARNING
// expect-unresolved: CDC-018
// expect-none: CDC-018 INFO
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) bin <= '0; else if (inc) bin <= bin + 4'd1;
  always_ff @(posedge clka or negedge rst_n) begin
    if (!rst_n) begin
      gray <= 4'd0;
      gray <= 4'd15;
    end else gray <= bin ^ (bin >> 1);
  end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s2, s1} <= '0; else {s2, s1} <= {s1, gray};
  assign q = s2;
endmodule
