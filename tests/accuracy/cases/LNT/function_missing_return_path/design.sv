module opcode_decoder (
  input  logic [2:0] opcode,
  input  logic [7:0] operand,
  output logic [7:0] result
);
  function automatic logic [7:0] decode_value(input logic [2:0] op,
                                               input logic [7:0] value);
    case (op)
      3'd0: decode_value = value;
      3'd1: decode_value = value + 8'd1;
      3'd2: decode_value = value - 8'd1;
      3'd3: decode_value = {value[3:0], value[7:4]};
      3'd4: decode_value = value & 8'hf0;
      3'd5: decode_value = value | 8'h0f;
    endcase
  endfunction

  always_comb begin
    result = decode_value(opcode, operand);
  end
endmodule

module top (
  input  logic [2:0] opcode,
  input  logic [7:0] operand,
  output logic [7:0] result
);
  opcode_decoder u_decoder (
    .opcode(opcode),
    .operand(operand),
    .result(result)
  );
endmodule
