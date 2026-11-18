// Instruction class from the top opcode nibble.
typedef enum logic [1:0] {
  CLS_ALU = 2'd0,
  CLS_MEM = 2'd1,
  CLS_BR  = 2'd2,
  CLS_SYS = 2'd3
} cls_t;

module top (
  input  logic [7:0] instr,
  output cls_t       cls,
  output logic       is_mem,
  output logic       is_ctrl
);
  always_comb begin
    casez (instr[7:4])
      4'b00??: cls = CLS_ALU;
      4'b01??: cls = CLS_MEM;
      4'b10??: cls = CLS_BR;
      4'b11??: cls = CLS_SYS;
    endcase
  end

  assign is_mem  = (cls == CLS_MEM);
  assign is_ctrl = (cls == CLS_BR) | (cls == CLS_SYS);
endmodule
