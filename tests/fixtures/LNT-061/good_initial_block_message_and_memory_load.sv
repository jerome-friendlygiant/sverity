// sverity-args: --top top
// An initial block runs once at time zero: a message there is ignored by synthesis and the memory file read is the FPGA load idiom.
// expect: LNT-061 INFO 9
// expect: LNT-061 INFO 10
// expect-none: LNT-061 WARNING
module top(input logic clk, input logic [1:0] a, output logic [7:0] q);
  logic [7:0] mem [0:3];
  initial begin
    $display("loading");
    $readmemh("rom.hex", mem);
  end
  always_ff @(posedge clk) q <= mem[a];
endmodule
