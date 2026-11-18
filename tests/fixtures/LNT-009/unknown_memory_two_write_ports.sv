// sverity-args: --top top
// The FPGA true-dual-port RAM template: two clocks each write the memory at their own address. Whether the addresses
// collide is not determinable here and the target needs a true dual-port RAM, so the overlap is Unresolved, not a proven conflict.
// expect: LNT-009 WARNING 9
module top(input logic clka, clkb, wea, web, input logic [3:0] addra, addrb, input logic [7:0] dia, dib,
           output logic [7:0] doa, dob);
  logic [7:0] ram [0:15];
  always @(posedge clka) begin if (wea) ram[addra] <= dia; doa <= ram[addra]; end
  always @(posedge clkb) begin if (web) ram[addrb] <= dib; dob <= ram[addrb]; end
endmodule
