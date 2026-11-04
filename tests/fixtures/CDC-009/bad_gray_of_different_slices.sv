// sverity-args: --top top --sdc @DIR@/bad_gray_of_different_slices.sdc
// XOR of two different slices of one counter is not Gray: from 3 to 4, three bits change.
// expect: CDC-009 WARNING 9
// expect-none: CDC-018
module top(input logic a, input logic b, output logic [3:0] q);
  logic [7:0] binary;
  logic [3:0] g, s1;
  always_ff @(posedge a) begin binary <= binary + 1'b1; g <= binary[3:0] ^ (binary[7:4] >> 1); end
  always_ff @(posedge b) begin s1 <= g; q <= s1; end
endmodule
