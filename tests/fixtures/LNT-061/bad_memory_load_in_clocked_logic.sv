// sverity-args: --top top
// Lookalike of good_initial_block_message_and_memory_load: outside an initial block the file read is simulation behaviour synthesis lacks.
// revert: $readmemh("rom.hex", mem); => mem[0] <= 8'h5a;
// revert-expect-none: LNT-061
// expect: LNT-061 WARNING 9
module top(input logic clk, input logic [1:0] a, output logic [7:0] q);
  logic [7:0] mem [0:3];
  always_ff @(posedge clk) begin
    $readmemh("rom.hex", mem);
    q <= mem[a];
  end
endmodule
