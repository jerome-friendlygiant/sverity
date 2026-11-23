// sverity-args: --top top --goal cdc --sdc @DIR@/nested_empty_pin_collection.sdc
// expect-unresolved: CDC-018
// expect-none: CDC-018 INFO
module top(input logic clka, clkb, rst_n, inc, output logic [3:0] q);
  logic [3:0] bin, gray, s1, s2;
  always_ff @(posedge clka or negedge rst_n) begin
    if (!rst_n) begin bin <= '0; gray <= '0; end
    else begin
      if (inc) bin <= bin + 4'd1;
      gray <= bin ^ (bin >> 1);
    end
  end
  always_ff @(posedge clkb or negedge rst_n) begin
    if (!rst_n) begin s1 <= '0; s2 <= '0; end
    else begin s1 <= gray; s2 <= s1; end
  end
  assign q = s2;
endmodule
