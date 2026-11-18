// Fetch program counter. After reset it starts at the boot vector supplied
// by the SoC integration; afterwards it follows jumps or advances by a word.
module top (
  input  logic        clk,
  input  logic        rst_n,
  input  logic [31:0] boot_addr_i,
  input  logic        jump_i,
  input  logic [31:0] jump_addr_i,
  output logic [31:0] pc_o
);
  logic [31:0] pc_q;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)      pc_q <= boot_addr_i;
    else if (jump_i) pc_q <= jump_addr_i;
    else             pc_q <= pc_q + 32'd4;
  end

  assign pc_o = pc_q;
endmodule
