# typed: true
extend T::Helpers
extend T::Generic

type_parameters(:T)
  .sig(blk: T.proc.returns(T.type_parameter(:T)))
  .returns(T.type_parameter(:T))
def callit(&blk)
  blk.call
end

def test_it
  callit do
    raise

    puts :dead # error: This code is unreachable
  end

  # Test that we are able to propagate from "blk never returns" to
  # "callit never returns"
  puts :dead # error: This code is unreachable
end
